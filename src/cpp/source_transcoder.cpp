#include "source_transcoder.h"
#include "source.h"
#include <media-io/video-frame.h>
#include <util/platform.h>

#define VIDEO_RESET_THRESHOLD 1000000000
#define VIDEO_SMOOTH_THRESHOLD 2000000
#define AUDIO_RESET_THRESHOLD 2000000000
#define AUDIO_SMOOTH_THRESHOLD 70000000
#define AUDIO_MAX_TIMESTAMP_BUFFER 1000000000

struct ts_info {
    uint64_t start;
    uint64_t end;
};

static inline uint64_t uint64_diff(uint64_t ts1, uint64_t ts2) {
    return (ts1 < ts2) ? (ts2 - ts1) : (ts1 - ts2);
}

SourceTranscoder::SourceTranscoder() :
        source(nullptr),
        output(nullptr),
        video(nullptr),
        frame_buf(),
        frame_buf_mutex(),
        video_scaler(nullptr),
        last_video_time(0),
        last_frame_ts(0),
        video_stop(false),
        video_thread(),
        audio(nullptr),
        audio_buf(),
        audio_buf_mutex(),
        audio_timestamp_buf(),
        audio_time(0),
        last_audio_time(0),
        timing_adjust(0),
        timing_mutex() {
}

void SourceTranscoder::start(Source *s) {
    source = s;
    output = new Output(source->settings->output);

    // video output
    obs_video_info ovi = {};
    obs_get_video_info(&ovi);

    video_output_info voi = {};
    std::string videoOutputName = std::string("source_video_output_") + source->id;
    voi.name = videoOutputName.c_str();
    voi.format = VIDEO_FORMAT_BGRA;
    voi.width = source->settings->output->width;
    voi.height = source->settings->output->height;
    voi.fps_num = ovi.fps_num;
    voi.fps_den = ovi.fps_den;
    voi.cache_size = 16;
    video_output_open(&video, &voi);

    video_thread = std::thread(&SourceTranscoder::video_output_callback, this);

    // audio output
    for (auto &buf : audio_buf) {
        circlebuf_init(&buf);
    }

    obs_audio_info oai = {};
    obs_get_audio_info(&oai);

    audio_output_info aoi = {};
    std::string audioOutputName = std::string("source_audio_output_") + source->id;
    aoi.name = audioOutputName.c_str();
    aoi.samples_per_sec = oai.samples_per_sec;
    aoi.format = AUDIO_FORMAT_FLOAT_PLANAR;
    aoi.speakers = oai.speakers;
    aoi.input_callback = audio_output_callback;
    aoi.input_param = this;

    obs_source_add_audio_capture_callback(source->obs_source, audio_capture_callback, this);

    audio_output_open(&audio, &aoi);

    output->start(video, audio);

    signal_handler_t *handler = obs_source_get_signal_handler(source->obs_source);
    signal_handler_connect(handler, "media_get_frame", source_media_get_frame_callback, this);
}

void SourceTranscoder::stop() {
    signal_handler_t *handler = obs_source_get_signal_handler(source->obs_source);
    signal_handler_disconnect(handler, "media_get_frame", source_media_get_frame_callback, this);

    output->stop();
    delete output;
    output = nullptr;

    video_stop = true;
    video_thread.join();
    video_stop = false;

    video_output_stop(video);
    video_output_close(video);

    if (video_scaler) {
        video_scaler_destroy(video_scaler);
        video_scaler = nullptr;
    }

    reset_video();

    last_video_time = 0;
    last_frame_ts = 0;

    // audio stop
    obs_source_remove_audio_capture_callback(source->obs_source, audio_capture_callback, this);

    audio_output_close(audio);

    reset_audio();

    audio_time = 0;
    last_audio_time = 0;
    timing_adjust = 0;
}

void SourceTranscoder::source_media_get_frame_callback(void *param, calldata_t *data) {
    auto transcoder = (SourceTranscoder *) param;
    auto *frame = (obs_source_frame *) calldata_ptr(data, "frame");

    // create video scaler after first frame is received
    if (!transcoder->video_scaler) {
        transcoder->create_video_scaler(frame);
    }

    obs_source_frame *new_frame = obs_source_frame_create(frame->format, frame->width, frame->height);
    obs_source_frame_copy(new_frame, frame);

    transcoder->frame_buf_mutex.lock();

    // clear frame buffer, only keep latest frame
    if (transcoder->last_frame_ts &&
        uint64_diff(transcoder->last_frame_ts, new_frame->timestamp) > VIDEO_RESET_THRESHOLD) {
        blog(LOG_INFO, "[%s] video timestamp reset: %llu -> %llu",
             transcoder->source->id.c_str(), transcoder->last_frame_ts, new_frame->timestamp);
        transcoder->reset_video();
    }

    circlebuf_push_back(&transcoder->frame_buf, &new_frame, sizeof(void *));
    transcoder->last_frame_ts = new_frame->timestamp;

    transcoder->frame_buf_mutex.unlock();
}

void SourceTranscoder::video_output_callback(void *param) {
    auto *transcoder = (SourceTranscoder *) param;
    const struct video_output_info *voi = video_output_get_info(transcoder->video);
    uint64_t interval = util_mul_div64(1000000000UL, voi->fps_den, voi->fps_num);
    transcoder->last_video_time = os_gettime_ns();

    while (!transcoder->video_stop) {
        uint64_t video_time = transcoder->last_video_time + interval;
        uint32_t count;
        if (os_sleepto_ns(video_time)) {
            count = 1;
        } else {
            count = (int) ((os_gettime_ns() - transcoder->last_video_time) / interval);
            video_time = transcoder->last_video_time + interval * count;
        }

        transcoder->frame_buf_mutex.lock();
        auto frame = transcoder->get_closest_frame(video_time);
        if (frame) {
            transcoder->timing_mutex.lock();
            transcoder->timing_adjust = video_time - frame->timestamp;
            transcoder->timing_mutex.unlock();
            struct video_frame output_frame = {};
            if (count > 1) {
                blog(LOG_INFO, "[%s] video lagged: %d", transcoder->source->id.c_str(), count);
            }
            if (video_output_lock_frame(transcoder->video, &output_frame, count, video_time)) {
                video_scaler_scale(
                        transcoder->video_scaler,
                        output_frame.data,
                        output_frame.linesize,
                        frame->data,
                        frame->linesize
                );
                video_output_unlock_frame(transcoder->video);
            }
        }
        transcoder->last_video_time = video_time;
        transcoder->frame_buf_mutex.unlock();
    }
}

void SourceTranscoder::audio_capture_callback(void *param, obs_source_t *source, const struct audio_data *audio_data,
                                              bool muted) {
    UNUSED_PARAMETER(source);
    UNUSED_PARAMETER(muted);

    auto transcoder = (SourceTranscoder *) param;

    size_t channels = audio_output_get_channels(transcoder->audio);
    size_t rate = audio_output_get_sample_rate(transcoder->audio);

    transcoder->timing_mutex.lock();
    uint64_t timing_adjust = transcoder->timing_adjust;
    transcoder->timing_mutex.unlock();

    if (!timing_adjust) {
        return;
    }

    transcoder->audio_buf_mutex.lock();

    uint64_t current_audio_time = audio_data->timestamp + timing_adjust;
    size_t audio_size = audio_data->frames * sizeof(float);

    // if audio time output range, reset audio
    if (!transcoder->audio_time || current_audio_time < transcoder->audio_time ||
        current_audio_time - transcoder->audio_time > AUDIO_RESET_THRESHOLD) {
        blog(LOG_INFO, "[%s] audio timestamp reset %llu -> %llu",
             transcoder->source->id.c_str(), current_audio_time, transcoder->audio_time);
        transcoder->reset_audio();
        transcoder->audio_time = current_audio_time;
        transcoder->last_audio_time = current_audio_time;
    }

    uint64_t diff = uint64_diff(transcoder->last_audio_time, current_audio_time);
    if (diff > AUDIO_SMOOTH_THRESHOLD) {
        blog(LOG_INFO, "[%s] audio buffer placement: %llu", transcoder->source->id.c_str(), diff);
        size_t buf_placement = ns_to_audio_frames(rate, current_audio_time - transcoder->audio_time) * sizeof(float);
        for (size_t i = 0; i < channels; i++) {
            circlebuf_place(&transcoder->audio_buf[i], buf_placement, audio_data->data[i], audio_size);
            circlebuf_pop_back(&transcoder->audio_buf[i], nullptr,
                               transcoder->audio_buf[i].size - (buf_placement + audio_size));
        }
        transcoder->last_audio_time = current_audio_time;
    } else {
        for (size_t i = 0; i < channels; i++) {
            circlebuf_push_back(&transcoder->audio_buf[i], audio_data->data[i], audio_size);
        }
    }

    transcoder->last_audio_time += audio_frames_to_ns(rate, audio_data->frames);
    transcoder->audio_buf_mutex.unlock();
}

bool SourceTranscoder::audio_output_callback(
        void *param,
        uint64_t start_ts_in,
        uint64_t end_ts_in,
        uint64_t *out_ts,
        uint32_t mixers,
        struct audio_output_data *mixes) {
    UNUSED_PARAMETER(mixers);

    auto transcoder = (SourceTranscoder *) param;
    size_t channels = audio_output_get_channels(transcoder->audio);
    size_t rate = audio_output_get_sample_rate(transcoder->audio);
    ts_info ts = {start_ts_in, end_ts_in};

    transcoder->audio_buf_mutex.lock();

    circlebuf_push_back(&transcoder->audio_timestamp_buf, &ts, sizeof(ts));
    circlebuf_peek_front(&transcoder->audio_timestamp_buf, &ts, sizeof(ts));

    bool paused = obs_source_media_get_state(transcoder->source->obs_source) == OBS_MEDIA_STATE_PAUSED;
    bool result = false;

    if (!transcoder->audio_time || paused) {
        // audio stopped, reset audio and send mute
        transcoder->reset_audio();
        result = true;
    } else if (transcoder->audio_time >= ts.end) {
        // audio go forward, send mute
        blog(LOG_INFO, "[%s] audio go forward, audio time: %llu, ts.end: %llu",
             transcoder->source->id.c_str(), transcoder->audio_time, ts.end);
        result = true;
    } else {
        size_t buffer_size = transcoder->audio_buf[0].size;
        if (transcoder->audio_time < ts.start) {
            // trunc buffer
            size_t trunc_size = ns_to_audio_frames(rate, ts.start - transcoder->audio_time) * sizeof(float);
            if (buffer_size < trunc_size) {
                for (size_t ch = 0; ch < channels; ch++) {
                    circlebuf_pop_front(&transcoder->audio_buf[ch], nullptr, buffer_size);
                }
                buffer_size = 0;
                transcoder->audio_time += audio_frames_to_ns(rate, buffer_size / sizeof(float));
            } else {
                for (size_t ch = 0; ch < channels; ch++) {
                    circlebuf_pop_front(&transcoder->audio_buf[ch], nullptr, trunc_size);
                }
                buffer_size -= trunc_size;
                transcoder->audio_time = ts.start;
            }
        }
        if (transcoder->audio_time >= ts.start) {
            size_t start_frame = ns_to_audio_frames(rate, transcoder->audio_time - ts.start);
            size_t audio_size = (AUDIO_OUTPUT_FRAMES - start_frame) * sizeof(float);
            if (buffer_size >= audio_size) {
                for (size_t ch = 0; ch < channels; ch++) {
                    circlebuf_pop_front(&transcoder->audio_buf[ch], mixes[0].data[ch] + start_frame, audio_size);
                }
                transcoder->audio_time = ts.end;
                result = true;
            }
        }
        if (!result && (end_ts_in - ts.start >= AUDIO_MAX_TIMESTAMP_BUFFER)) {
            // audio lagged too much, reset audio and send mute
            blog(LOG_INFO, "[%s] audio timestamp buffer exceed limit: %llu, audio time: %llu, ts.end: %llu",
                 transcoder->source->id.c_str(), end_ts_in - ts.start, transcoder->audio_time, ts.end);
            transcoder->reset_audio();
            result = true;
        }
    }

    if (result) {
        circlebuf_pop_front(&transcoder->audio_timestamp_buf, nullptr, sizeof(ts));
    }

    transcoder->audio_buf_mutex.unlock();

    *out_ts = ts.start;
    return result;
}

void SourceTranscoder::create_video_scaler(obs_source_frame *frame) {
    const struct video_output_info *voi = video_output_get_info(video);
    struct video_scale_info src = {
            .format = frame->format,
            .width = frame->width,
            .height = frame->height,
            .range = frame->full_range ? VIDEO_RANGE_FULL : VIDEO_RANGE_DEFAULT,
            .colorspace = VIDEO_CS_DEFAULT
    };
    struct video_scale_info dest = {
            .format = voi->format,
            .width = voi->width,
            .height = voi->height,
            .range = VIDEO_RANGE_DEFAULT,
            .colorspace = VIDEO_CS_DEFAULT
    };

    int ret = video_scaler_create(&video_scaler, &dest, &src, VIDEO_SCALE_FAST_BILINEAR);
    if (ret != VIDEO_SCALER_SUCCESS) {
        throw std::runtime_error("Failed to create video scaler.");
    }
}

obs_source_frame *SourceTranscoder::get_closest_frame(uint64_t video_time) {
    if (!frame_buf.size) {
        return nullptr;
    }

    obs_source_frame *frame;
    circlebuf_peek_front(&frame_buf, &frame, sizeof(void *));

    if (!last_video_time) {
        last_video_time = video_time;
    }

    if (!last_frame_ts) {
        last_frame_ts = frame->timestamp;
    }

    uint64_t sys_offset = video_time - last_video_time;
    uint64_t frame_ts = sys_offset + last_frame_ts;
    while (frame_ts > frame->timestamp && frame_buf.size > sizeof(void *)) {
        if (frame_ts - frame->timestamp < VIDEO_SMOOTH_THRESHOLD) {
            break;
        }
        circlebuf_pop_front(&frame_buf, &frame, sizeof(void *));
        obs_source_frame_destroy(frame);
        circlebuf_peek_front(&frame_buf, &frame, sizeof(void *));
    }
    last_frame_ts = frame_ts;
    return frame;
}

void SourceTranscoder::reset_video() {
    while (frame_buf.size > 0) {
        obs_source_frame *frame = nullptr;
        circlebuf_pop_front(&frame_buf, &frame, sizeof(void *));
        obs_source_frame_destroy(frame);
    }
    last_frame_ts = 0;
}

void SourceTranscoder::reset_audio() {
    size_t channels = audio_output_get_channels(audio);
    for (size_t ch = 0; ch < channels; ch++) {
        circlebuf_pop_front(&audio_buf[ch], nullptr, audio_buf[ch].size);
    }
    audio_time = 0;
    last_audio_time = 0;
}
