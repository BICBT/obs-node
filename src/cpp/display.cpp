// Most of code in this file are copied from
// https://github.com/stream-labs/obs-studio-node/blob/staging/obs-studio-server/source/nodeobs_display.cpp

#include "display.h"
#include "./platform/platform.h"

#ifdef _WIN32
#include <windows.h>
#endif

#ifdef _WIN32
enum class SystemWorkerMessage : uint32_t
{
    CreateWindow  = WM_USER + 0,
    DestroyWindow = WM_USER + 1,
    StopThread    = WM_USER + 2,
};

struct message_answer
{
    HANDLE      event;
    bool        called  = false;
    bool        success = false;
    DWORD       errorCode;
    std::string errorMessage;

    message_answer()
    {
        event = CreateSemaphore(NULL, 0, INT32_MAX, NULL);
    }
    ~message_answer()
    {
        CloseHandle(event);
    }

    bool wait()
    {
        return WaitForSingleObject(event, 1) == WAIT_OBJECT_0;
    }

    bool try_wait()
    {
        return WaitForSingleObject(event, 0) == WAIT_OBJECT_0;
    }

    void signal()
    {
        ReleaseSemaphore(event, 1, NULL);
    }
};

struct CreateWindowMessageQuestion
{
    HWND     parentWindow;
    uint32_t width, height;
};

struct CreateWindowMessageAnswer : message_answer
{
    HWND windowHandle;
};

struct DestroyWindowMessageQuestion
{
    HWND window;
};

struct DestroyWindowMessageAnswer : message_answer
{};

void OBS::Display::SystemWorker()
{
    MSG message;
    PeekMessage(&message, NULL, WM_USER, WM_USER, PM_NOREMOVE);

    bool keepRunning = true;
    do {
        BOOL gotMessage = GetMessage(&message, NULL, 0, 0);
        if (gotMessage == 0) {
            continue;
        }
        if (gotMessage == -1) {
            break; // Some sort of error.
        }

        if (message.hwnd != NULL) {
            TranslateMessage(&message);
            DispatchMessage(&message);
            continue;
        }

        switch ((SystemWorkerMessage)message.message) {
            case SystemWorkerMessage::CreateWindow: {
                CreateWindowMessageQuestion* question = reinterpret_cast<CreateWindowMessageQuestion*>(message.wParam);
                CreateWindowMessageAnswer*   answer   = reinterpret_cast<CreateWindowMessageAnswer*>(message.lParam);

                BOOL enabled = FALSE;
                DwmIsCompositionEnabled(&enabled);
                DWORD windowStyle;

                if (IsWindows8OrGreater() || !enabled) {
                    windowStyle = WS_EX_LAYERED | WS_EX_TRANSPARENT | WS_EX_TOPMOST;
                } else {
                    windowStyle = WS_EX_LAYERED | WS_EX_TRANSPARENT | WS_EX_TOPMOST | WS_EX_COMPOSITED;
                }

                HWND newWindow = CreateWindowEx(
                        windowStyle,
                        TEXT("Win32DisplayClass"),
                        TEXT("SlobsChildWindowPreview"),
                        WS_VISIBLE | WS_POPUP | WS_CHILD,
                        0,
                        0,
                        question->width,
                        question->height,
                        NULL,
                        NULL,
                        NULL,
                        this);

                if (!newWindow) {
                    answer->success = false;
                    HandleWin32ErrorMessage(GetLastError());
                } else {
                    if (IsWindows8OrGreater() || !enabled) {
                        SetLayeredWindowAttributes(newWindow, 0, 255, LWA_ALPHA);
                    }

                    SetParent(newWindow, question->parentWindow);
                    answer->windowHandle = newWindow;
                    answer->success      = true;
                }

                answer->called = true;
                answer->signal();
                break;
            }
            case SystemWorkerMessage::DestroyWindow: {
                DestroyWindowMessageQuestion* question = reinterpret_cast<DestroyWindowMessageQuestion*>(message.wParam);
                DestroyWindowMessageAnswer*   answer   = reinterpret_cast<DestroyWindowMessageAnswer*>(message.lParam);

                if (!DestroyWindow(question->window)) {
                    auto error = GetLastError();

                    // We check for error 1400 because if this display is a projector, it is attached to a HTML DOM, so
                    // we cannot directly control its destruction since the HTML will probably do this concurrently,
                    // the DestroyWindow is allows to fail on this case, a better solution here woul be checking if this
                    // display is really a projector and do not attempt to destroy it (let the HTML do it for us).
                    if (error != 1400) {
                        answer->success = false;
                        HandleWin32ErrorMessage(error);
                    } else {
                        answer->success = true;
                    }

                } else {
                    answer->success = true;
                }

                answer->called = true;
                answer->signal();
                break;
            }
            case SystemWorkerMessage::StopThread: {
                keepRunning = false;
                break;
            }
        }
    } while (keepRunning);
}
#endif

#if defined(_WIN32)

bool OBS::Display::DisplayWndClassRegistered;

WNDCLASSEX OBS::Display::DisplayWndClassObj;

ATOM OBS::Display::DisplayWndClassAtom;

void OBS::Display::DisplayWndClass()
{
	if (DisplayWndClassRegistered)
		return;

	DisplayWndClassObj.cbSize = sizeof(WNDCLASSEX);
	DisplayWndClassObj.style  = CS_OWNDC | CS_NOCLOSE | CS_HREDRAW
	                           | CS_VREDRAW; // CS_DBLCLKS | CS_HREDRAW | CS_NOCLOSE | CS_VREDRAW | CS_OWNDC;
	DisplayWndClassObj.lpfnWndProc   = DisplayWndProc;
	DisplayWndClassObj.cbClsExtra    = 0;
	DisplayWndClassObj.cbWndExtra    = 0;
	DisplayWndClassObj.hInstance     = NULL; // HINST_THISCOMPONENT;
	DisplayWndClassObj.hIcon         = NULL;
	DisplayWndClassObj.hCursor       = NULL;
	DisplayWndClassObj.hbrBackground = NULL;
	DisplayWndClassObj.lpszMenuName  = NULL;
	DisplayWndClassObj.lpszClassName = TEXT("Win32DisplayClass");
	DisplayWndClassObj.hIconSm       = NULL;

	DisplayWndClassAtom = RegisterClassEx(&DisplayWndClassObj);
	if (DisplayWndClassAtom == NULL) {
		HandleWin32ErrorMessage(GetLastError());
	}

	DisplayWndClassRegistered = true;
}

LRESULT CALLBACK OBS::Display::DisplayWndProc(_In_ HWND hwnd, _In_ UINT uMsg, _In_ WPARAM wParam, _In_ LPARAM lParam)
{
	OBS::Display* self = nullptr;
	switch (uMsg) {
	case WM_NCHITTEST:
		return HTTRANSPARENT;
	}

	return DefWindowProc(hwnd, uMsg, wParam, lParam);
}

#endif

Display::Display(void *parentHandle, int scaleFactor, std::string &sourceName) {
    this->parentHandle = parentHandle;
    this->scaleFactor = scaleFactor;

    // create window for display
//    this->windowHandle = createDisplayWindow(parentHandle);

#if defined(_WIN32)
    DisplayWndClass();
#elif defined(__APPLE__)
#elif defined(__linux__) || defined(__FreeBSD__)
#endif

#ifdef WIN32
    worker = std::thread(std::bind(&OBS::Display::SystemWorker, this));
#endif

    // create display
    gs_init_data gs_init_data = {};
    gs_init_data.adapter = 0;
    gs_init_data.cx = 1;
    gs_init_data.cy = 1;
    gs_init_data.num_backbuffers = 1;
    gs_init_data.format = GS_RGBA;
    gs_init_data.zsformat = GS_ZS_NONE;
#ifdef _WIN32
    CreateWindowMessageQuestion question;
	CreateWindowMessageAnswer   answer;

	question.parentWindow = (HWND)windowHandle;
	question.width        = m_gsInitData.cx;
	question.height       = m_gsInitData.cy;
	while (!PostThreadMessage(
	    GetThreadId(worker.native_handle()),
	    (UINT)SystemWorkerMessage::CreateWindow,
	    reinterpret_cast<intptr_t>(&question),
	    reinterpret_cast<intptr_t>(&answer))) {
		Sleep(0);
	}

	if (!answer.try_wait()) {
		while (!answer.wait()) {
			if (answer.called)
				break;
			Sleep(0);
		}
	}

	if (!answer.success) {
		throw std::system_error(answer.errorCode, std::system_category(), answer.errorMessage);
	}
	m_ourWindow              = answer.windowHandle;
	m_parentWindow           = *reinterpret_cast<HWND*>(parentHandle);
	gs_init_data.window.hwnd = reinterpret_cast<void*>(m_ourWindow);
#elif __APPLE__
    gs_init_data.window.view = static_cast<objc_object *>(windowHandle);
#endif
    obs_display = obs_display_create(&gs_init_data, 0x0);

    if (!obs_display) {
        throw std::runtime_error("Failed to create the display");
    }

    // obs source
    obs_source = obs_get_source_by_name(sourceName.c_str());
    obs_source_inc_showing(obs_source);

    // draw callback
    obs_display_add_draw_callback(obs_display, displayCallback, this);
}

Display::~Display() {
    obs_display_remove_draw_callback(obs_display, displayCallback, this);
    if (obs_source) {
        obs_source_dec_showing(obs_source);
        obs_source_release(obs_source);
    }
    if (obs_display) {
        obs_display_destroy(obs_display);
    }
//    destroyWindow(windowHandle);

#ifdef _WIN32
    DestroyWindowMessageQuestion question;
	DestroyWindowMessageAnswer   answer;

	question.window = m_ourWindow;
	PostThreadMessage(
	    GetThreadId(worker.native_handle()),
	    (UINT)SystemWorkerMessage::DestroyWindow,
	    reinterpret_cast<intptr_t>(&question),
	    reinterpret_cast<intptr_t>(&answer));

	if (!answer.try_wait()) {
		while (!answer.wait()) {
			if (answer.called)
				break;
			Sleep(0);
		}
	}

	if (!answer.success) {
		std::cerr << "OBS::Display::~Display: " << answer.errorMessage << std::endl;
	}

	PostThreadMessage(GetThreadId(worker.native_handle()), (UINT)SystemWorkerMessage::StopThread, NULL, NULL);

	if (worker.joinable())
		worker.join();
#endif
}

void Display::move(int x, int y, int width, int height) {
    this->x = x;
    this->y = y;
    this->width = width;
    this->height = height;
    moveWindow(windowHandle, x, y, width, height);
    obs_display_resize(obs_display, width * scaleFactor, height * scaleFactor);
}

void Display::displayCallback(void *displayPtr, uint32_t cx, uint32_t cy) {
    auto *dp = static_cast<Display *>(displayPtr);

    // Get proper source/base size.
    uint32_t sourceW, sourceH;
    if (dp->obs_source) {
        sourceW = obs_source_get_width(dp->obs_source);
        sourceH = obs_source_get_height(dp->obs_source);
        if (sourceW == 0)
            sourceW = 1;
        if (sourceH == 0)
            sourceH = 1;
    } else {
        obs_video_info ovi = {};
        obs_get_video_info(&ovi);

        sourceW = ovi.base_width;
        sourceH = ovi.base_height;
        if (sourceW == 0)
            sourceW = 1;
        if (sourceH == 0)
            sourceH = 1;
    }

    gs_projection_push();

    gs_ortho(0.0f, (float)sourceW, 0.0f, (float)sourceH, -1, 1);

    // Source Rendering
    obs_source_t *source;
    if (dp->obs_source) {
        obs_source_video_render(dp->obs_source);
        /* If we want to draw guidelines, we need a scene,
         * not a transition. This may not be a scene which
         * we'll check later. */
        if (obs_source_get_type(dp->obs_source) == OBS_SOURCE_TYPE_TRANSITION) {
            source = obs_transition_get_active_source(dp->obs_source);
        } else {
            source = dp->obs_source;
            obs_source_addref(source);
        }
    } else {
        obs_render_main_texture();

        /* Here we assume that channel 0 holds the primary transition.
        * We also assume that the active source within that transition is
        * the scene that we need */
        obs_source_t *transition = obs_get_output_source(0);
        source = obs_transition_get_active_source(transition);
        obs_source_release(transition);
    }

    obs_source_release(source);
    gs_projection_pop();
}