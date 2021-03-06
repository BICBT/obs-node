#include "studio.h"
#include <filesystem>
#include <mutex>
#include <obs.h>

std::mutex scenes_mtx;
std::string Studio::obsPath;

Studio::Studio(Settings *settings) :
          settings(settings),
          currentScene(nullptr),
          outputs(),
          overlays() {
    for (auto o : settings->outputs) {
        outputs.push_back(new Output(o));
    }
}

Studio::~Studio() {
    for (auto output : outputs) {
        delete output;
    }
}

void Studio::startup() {
    auto currentWorkDir = std::filesystem::current_path();

    // Change work directory to obs bin path to setup obs properly.
    blog(LOG_INFO, "Set work directory to %s for loading obs data", getObsBinPath().c_str());
    std::filesystem::current_path(getObsBinPath());

    auto restore = [&] {
        std::filesystem::current_path(currentWorkDir);
    };

    try {
        obs_startup("en-US", nullptr, nullptr);
        if (!obs_initialized()) {
            throw std::runtime_error("Failed to startup obs studio.");
        }

        // reset video
        if (settings->video) {
            obs_video_info ovi = {};
            memset(&ovi, 0, sizeof(ovi));
            ovi.adapter = 0;
#ifdef _WIN32
            ovi.graphics_module = "libobs-opengl.dll";
#else
            ovi.graphics_module = "libobs-opengl.so";
#endif
            ovi.output_format = VIDEO_FORMAT_NV12;
            ovi.fps_num = settings->video->fpsNum;
            ovi.fps_den = settings->video->fpsDen;
            ovi.base_width = settings->video->baseWidth;
            ovi.base_height = settings->video->baseHeight;
            ovi.output_width = settings->video->outputWidth;
            ovi.output_height = settings->video->outputHeight;
            ovi.gpu_conversion = true; // always be true for the OBS issue

            int result = obs_reset_video(&ovi);
            if (result != OBS_VIDEO_SUCCESS) {
                throw std::runtime_error("Failed to reset video");
            }
        }

        // reset audio
        if (settings->audio) {
            obs_audio_info oai = {};
            memset(&oai, 0, sizeof(oai));
            oai.samples_per_sec = settings->audio->sampleRate;
            oai.speakers = SPEAKERS_STEREO;
            if (!obs_reset_audio(&oai)) {
                throw std::runtime_error("Failed to reset audio");
            }
        }

        // load modules
#ifdef _WIN32
        loadModule(getObsPluginPath() + "\\image-source.dll", getObsPluginDataPath() + "\\image-source");
        loadModule(getObsPluginPath() + "\\obs-ffmpeg.dll", getObsPluginDataPath() + "\\obs-ffmpeg");
        loadModule(getObsPluginPath() + "\\obs-transitions.dll", getObsPluginDataPath() + "\\obs-transitions");
        loadModule(getObsPluginPath() + "\\rtmp-services.dll", getObsPluginDataPath() + "\\rtmp-services");
        loadModule(getObsPluginPath() + "\\obs-x264.dll", getObsPluginDataPath() + "\\obs-x264");
        loadModule(getObsPluginPath() + "\\obs-outputs.dll", getObsPluginDataPath() + "\\obs-outputs");
        loadModule(getObsPluginPath() + "\\text-freetype2.dll", getObsPluginDataPath() + "\\text-freetype2");
#else
        loadModule(getObsPluginPath() + "/image-source.so", getObsPluginDataPath() + "/image-source");
        loadModule(getObsPluginPath() + "/obs-ffmpeg.so", getObsPluginDataPath() + "/obs-ffmpeg");
        loadModule(getObsPluginPath() + "/obs-transitions.so", getObsPluginDataPath() + "/obs-transitions");
        loadModule(getObsPluginPath() + "/rtmp-services.so", getObsPluginDataPath() + "/rtmp-services");
        loadModule(getObsPluginPath() + "/obs-x264.so", getObsPluginDataPath() + "/obs-x264");
        loadModule(getObsPluginPath() + "/obs-outputs.so", getObsPluginDataPath() + "/obs-outputs");
        loadModule(getObsPluginPath() + "/text-freetype2.so", getObsPluginDataPath() + "/text-freetype2");
#endif

        obs_post_load_modules();

        for (auto output : outputs) {
            output->start(obs_get_video(), obs_get_audio());
        }

        restore();

    } catch (...) {
        restore();
        throw;
    }
}

void Studio::shutdown() {
    for (auto output : outputs) {
        output->stop();
    }
    obs_shutdown();
    if (obs_initialized()) {
        throw std::runtime_error("Failed to shutdown obs studio.");
    }
}

void Studio::addScene(std::string &sceneId) {
    std::unique_lock<std::mutex> lock(scenes_mtx);
    int index = (int)scenes.size();
    auto scene = new Scene(sceneId, index, settings);
    scenes[sceneId] = scene;
}

void Studio::addSource(std::string &sceneId, std::string &sourceId, std::shared_ptr<SourceSettings> &settings) {
    findScene(sceneId)->addSource(sourceId, settings);
}

Source *Studio::findSource(std::string &sceneId, std::string &sourceId) {
    return findScene(sceneId)->findSource(sourceId);
}

void Studio::addDSK(std::string &id, std::string &position, std::string &url, int left, int top, int width, int height) {
    auto found = dsks.find(id);
    if (found != dsks.end()) {
        throw std::logic_error("Dsk " + id + " already existed");
    }
    auto *dsk = new Dsk(id, position, url, left, top, width, height);
    dsks[id] = dsk;
}

void Studio::switchToScene(std::string &sceneId, std::string &transitionType, int transitionMs) {
    Scene *next = findScene(sceneId);

    if (next == currentScene) {
        blog(LOG_INFO, "Same with current scene, no need to switch, skip.");
        return;
    }

    blog(LOG_INFO, "Start transition: %s -> %s", (currentScene ? currentScene->getId().c_str() : ""),
         next->getId().c_str());

    // Find or create transition
    auto it = transitions.find(transitionType);
    if (it == transitions.end()) {
        transitions[transitionType] = obs_source_create(transitionType.c_str(), transitionType.c_str(), nullptr,
                                                        nullptr);
    }

    obs_source_t *transition = transitions[transitionType];
    if (currentScene) {
        obs_transition_set(transition, obs_scene_get_source(currentScene->getObsOutputScene(dsks)));
    }

    obs_set_output_source(0, transition);

    bool ret = obs_transition_start(
            transition,
            OBS_TRANSITION_MODE_AUTO,
            transitionMs,
            obs_scene_get_source(next->getObsOutputScene(dsks))
    );

    if (!ret) {
        throw std::runtime_error("Failed to start transition.");
    }

    currentScene = next;
}

void Studio::loadModule(const std::string &binPath, const std::string &dataPath) {
    obs_module_t *module = nullptr;
    int code = obs_open_module(&module, binPath.c_str(), dataPath.c_str());
    if (code != MODULE_SUCCESS) {
        throw std::runtime_error("Failed to load module '" + binPath + "'");
    }
    if (!obs_init_module(module)) {
        throw std::runtime_error("Failed to load module '" + binPath + "'");
    }
}

void Studio::setObsPath(std::string &obsPath) {
    Studio::obsPath = obsPath;
}

void Studio::createDisplay(std::string &displayName, void *parentHandle, int scaleFactor, std::string &sourceId) {
    auto found = displays.find(displayName);
    if (found != displays.end()) {
        throw std::logic_error("Display " + displayName + " already existed");
    }
    auto *display = new Display(parentHandle, scaleFactor, sourceId);
    displays[displayName] = display;
}

void Studio::destroyDisplay(std::string &displayName) {
    auto found = displays.find(displayName);
    if (found == displays.end()) {
        throw std::logic_error("Can't find display: " + displayName);
    }
    Display *display = found->second;
    displays.erase(displayName);
    delete display;
}

void Studio::moveDisplay(std::string &displayName, int x, int y, int width, int height) {
    auto found = displays.find(displayName);
    if (found == displays.end()) {
        throw std::logic_error("Can't find display: " + displayName);
    }
    found->second->move(x, y, width, height);
}

bool Studio::getAudioWithVideo() {
    return obs_get_audio_with_video();
}

void Studio::setAudioWithVideo(bool audioWithVideo) {
    obs_set_audio_with_video(audioWithVideo);
}

void Studio::setPgmMonitor(bool pgmMonitor) {
    obs_set_pgm_audio_monitor(pgmMonitor);
}

float Studio::getMasterVolume() {
    return obs_mul_to_db(obs_get_master_volume());
}

void Studio::setMasterVolume(float volume) {
    // input volume is dB
    obs_set_master_volume(obs_db_to_mul(volume));
}

void Studio::addOverlay(Overlay *overlay) {
    if (overlays.find(overlay->id) != overlays.end()) {
        throw std::logic_error("Overlay: " + overlay->id + " already existed");
    }
    overlays[overlay->id] = overlay;
}

void Studio::removeOverlay(const std::string &overlayId) {
    if (overlays.find(overlayId) == overlays.end()) {
        throw std::logic_error("Can't find overlay: " + overlayId);
    }
    auto overlay = overlays[overlayId];
    if (overlay->index > -1) {
        overlay->down();
    }
    delete overlay;
    overlays.erase(overlayId);
}

void Studio::upOverlay(const std::string &overlayId) {
    if (overlays.find(overlayId) == overlays.end()) {
        throw std::logic_error("Can't find overlay: " + overlayId);
    }
    // find max overlay index
    int index = -1;
    for (const auto& entry : overlays) {
        if (entry.second->index > index) {
            index = entry.second->index;
        }
    }
    overlays[overlayId]->up(index + 1);
}

void Studio::downOverlay(const std::string &overlayId) {
    if (overlays.find(overlayId) == overlays.end()) {
        throw std::logic_error("Can't find overlay: " + overlayId);
    }
    overlays[overlayId]->down();
}

std::map<std::string, Overlay *> &Studio::getOverlays() {
    return overlays;
}

Scene *Studio::findScene(std::string &sceneId) {
    auto it = scenes.find(sceneId);
    if (it == scenes.end()) {
        throw std::invalid_argument("Can't find scene " + sceneId);
    }
    return it->second;
}

std::string Studio::getObsBinPath() {
#ifdef _WIN32
    return obsPath + "\\bin\\64bit";
#elif __linux__
    return obsPath + "/bin/64bit";
#else
    return obsPath + "/bin";
#endif
}

std::string Studio::getObsPluginPath() {
#ifdef _WIN32
    // Obs plugin path is same with bin path, due to SetDllDirectoryW called in obs-studio/libobs/util/platform-windows.c.
    return obsPath + "\\bin\\64bit";
#elif __linux__
    return obsPath + "/obs-plugins/64bit";
#else
    return obsPath + "/obs-plugins";
#endif
}

std::string Studio::getObsPluginDataPath() {
#ifdef _WIN32
    return obsPath + "\\data\\obs-plugins";
#else
    return obsPath + "/data/obs-plugins";
#endif
}
