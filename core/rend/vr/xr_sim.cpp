/*
	OpenXR simulator (hotd2-vr). See xr_sim.h.

	One instance, one session, its spaces, swapchains and actions, all on Flycast's render
	thread, the one xr_host calls from; handles are small integers (an index plus 1). What the
	harness sends (udpLine, the UDP thread) is kept under a mutex and taken by xrSyncActions
	(inputs) and the locate calls (poses).

	Copyright 2026 mikermak. This file is part of Flycast and is distributed under the GNU GPL v2 or later.
*/
#include "xr_sim.h"

#if defined(USE_OPENXR) && !defined(__ANDROID__)
#include "rend/gles/gles.h"
#include <windows.h>
#include <unknwn.h>
#define XR_USE_PLATFORM_WIN32
#define XR_USE_GRAPHICS_API_OPENGL
#include <openxr/openxr_platform.h>

#include "cfg/option.h"
#include "log/Log.h"
#include "types.h"
#include "ui/imgui_driver.h"

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>
#include <atomic>
#include <charconv>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <deque>
#include <map>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace vr::xr::sim
{
namespace
{

constexpr const char *RuntimeName = "Flycast XR simulator";
// a Quest 3 eye: 832x720, 52 degrees each side and 48 up and down (tan 1.2799 and 1.1106, the
// findings' vr.WindowTanX/Y), the eyes 64 mm apart
constexpr int EyeWidth = 832, EyeHeight = 720;
constexpr float Degree = 3.14159265f / 180.f;
constexpr float FovAcross = 52.f * Degree, FovUpDown = 48.f * Degree;
constexpr float HalfIpd = 0.032f;
constexpr int RefreshRate = 72;
constexpr u32 SwapchainLength = 3;
constexpr size_t None = ~(size_t)0;

struct Pose
{
	glm::vec3 position { 0.f };
	glm::quat orientation { 1.f, 0.f, 0.f, 0.f };
};

Pose compose(const Pose& a, const Pose& b) {
	return { a.position + a.orientation * b.position, a.orientation * b.orientation };
}
// b in a's space
Pose relative(const Pose& a, const Pose& b)
{
	const glm::quat inverse = glm::conjugate(a.orientation);
	return { inverse * (b.position - a.position), inverse * b.orientation };
}
Pose fromXr(const XrPosef& p) {
	return { { p.position.x, p.position.y, p.position.z }, glm::quat(p.orientation.w, p.orientation.x, p.orientation.y, p.orientation.z) };
}
XrPosef toXr(const Pose& p)
{
	XrPosef r;
	r.position = { p.position.x, p.position.y, p.position.z };
	r.orientation = { p.orientation.x, p.orientation.y, p.orientation.z, p.orientation.w };
	return r;
}
// Facing -Z turned by yaw (about +Y: left), then pitch (about +X: up), then roll (about the
// view axis), in degrees.
Pose makePose(float x, float y, float z, float yaw, float pitch, float roll)
{
	const glm::quat q = glm::angleAxis(yaw * Degree, glm::vec3(0.f, 1.f, 0.f))
			* glm::angleAxis(pitch * Degree, glm::vec3(1.f, 0.f, 0.f))
			* glm::angleAxis(roll * Degree, glm::vec3(0.f, 0.f, -1.f));
	return { { x, y, z }, q };
}

//
// What the harness sets (udpLine, the UDP thread)
//
constexpr int Head = 0, LeftHand = 1, RightHand = 2;
const char *const PoseNames[] { "head", "left", "right" };
std::mutex inputMutex;
std::map<std::string, float> liveInputs;		// by input path ("/user/hand/right/input/a/click")
Pose livePoses[3] {
	makePose(0.f, 1.6f, 0.f, 0.f, 0.f, 0.f),
	makePose(-0.18f, 1.3f, -0.35f, 0.f, 0.f, 0.f),
	makePose(0.18f, 1.3f, -0.35f, 0.f, 0.f, 0.f),
};
std::atomic<bool> wantHeadTracked { true }; // XRTRACK head 0/1, independent of controller tracking
std::atomic<bool> wantFocus { true };		// XRSTATE focused / visible

Pose livePose(int who)
{
	std::lock_guard<std::mutex> lock(inputMutex);
	return livePoses[who];
}

//
// The runtime's state (render thread)
//
struct Space
{
	bool used = true;
	XrReferenceSpaceType reference = XR_REFERENCE_SPACE_TYPE_LOCAL;
	size_t action = None;		// an action space: its pose action
	XrPath subaction = XR_NULL_PATH;
	Pose offset;
};
struct ActionSet
{
	bool used = true;
	std::string name;
};
struct Action
{
	bool used = true;
	size_t set;
	std::string name;
	XrActionType type;
};
struct Swapchain
{
	bool used = true;
	int width = 0, height = 0;
	std::vector<GLuint> images;
	u32 next = 0;
	u32 acquired = 0;
	u32 released = ~0u;		// the image last given back (the one shown)
};

struct State
{
	bool instance = false;
	bool renderModelEnabled = false;	// the app enabled XR_FB_render_model (its functions handed out)
	std::vector<std::string> paths;		// XrPath n is paths[n - 1]
	std::map<XrPath, std::vector<XrActionSuggestedBinding>> suggested;		// by interaction profile
	std::vector<ActionSet> actionSets;
	std::vector<Action> actions;
	bool requirementsAsked = false;

	bool session = false;
	XrSessionState sessionState = XR_SESSION_STATE_UNKNOWN;
	bool running = false;
	bool exitRequested = false;
	std::deque<XrEventDataBuffer> events;
	std::vector<Space> spaces;
	std::vector<Swapchain> swapchains;
	bool attached = false;
	std::vector<std::pair<size_t, std::string>> bindings;		// (action, input path) of the current profile
	std::map<std::string, float> inputs, lastInputs;		// as of the last xrSyncActions and the one before
	bool syncFocused = false;
	// the interaction profile bound: as a runtime does, at the first xrSyncActions while focused
	// (XR_TYPE_EVENT_DATA_INTERACTION_PROFILE_CHANGED sent then); no render model before
	bool profileBound = false;
	XrTime syncTime = 0;
	std::chrono::steady_clock::time_point nextFrame;
	GLuint showFbo = 0;
};
State st;

template<typename H> H toHandle(size_t index) { return (H)(uintptr_t)(index + 1); }
template<typename H> size_t fromHandle(H handle) { return (size_t)(uintptr_t)handle - 1; }	// (null: None)

const auto epoch = std::chrono::steady_clock::now();
XrTime xrTime(std::chrono::steady_clock::time_point t) {
	return std::chrono::duration_cast<std::chrono::nanoseconds>(t - epoch).count() + 1000000000;
}

XrPath toPath(const std::string& s)
{
	for (size_t i = 0; i < st.paths.size(); i++)
		if (st.paths[i] == s)
			return i + 1;
	st.paths.push_back(s);
	return st.paths.size();
}
const std::string& pathString(XrPath p)
{
	static const std::string none;
	return p >= 1 && p <= st.paths.size() ? st.paths[p - 1] : none;
}
std::string profilePath() {
	return "/interaction_profiles/" + config::VrXrSimProfile.get();
}

// the two-call idiom
template<typename T, typename F>
XrResult fill(uint32_t capacity, uint32_t *count, T *out, uint32_t n, F&& set)
{
	if (count == nullptr)
		return XR_ERROR_VALIDATION_FAILURE;
	*count = n;
	if (capacity == 0)
		return XR_SUCCESS;
	if (capacity < n || out == nullptr)
		return XR_ERROR_SIZE_INSUFFICIENT;
	for (uint32_t i = 0; i < n; i++)
		set(out[i], i);
	return XR_SUCCESS;
}

void pushEvent(const void *event, size_t size)
{
	XrEventDataBuffer buffer { XR_TYPE_EVENT_DATA_BUFFER };
	memcpy(&buffer, event, size);
	st.events.push_back(buffer);
}

void setState(XrSessionState state)
{
	st.sessionState = state;
	XrEventDataSessionStateChanged event { XR_TYPE_EVENT_DATA_SESSION_STATE_CHANGED };
	event.session = toHandle<XrSession>(0);
	event.state = state;
	event.time = xrTime(std::chrono::steady_clock::now());
	pushEvent(&event, sizeof(event));
}

// XRSTATE: focus lost (a system menu over the app) and back
void followFocus()
{
	const bool want = wantFocus;
	if (st.sessionState == XR_SESSION_STATE_FOCUSED && !want)
	{
		NOTICE_LOG(RENDERER, "XRSIM: focus lost (VISIBLE)");
		setState(XR_SESSION_STATE_VISIBLE);
	}
	else if (st.sessionState == XR_SESSION_STATE_VISIBLE && st.running && !st.exitRequested && want)
	{
		NOTICE_LOG(RENDERER, "XRSIM: focus back (FOCUSED)");
		setState(XR_SESSION_STATE_FOCUSED);
	}
}

void deleteSession()
{
	for (Swapchain& c : st.swapchains)
		if (c.used && !c.images.empty())
			glcache.DeleteTextures((GLsizei)c.images.size(), c.images.data());
	if (st.showFbo != 0)
		glDeleteFramebuffers(1, &st.showFbo);
	st.showFbo = 0;
	st.swapchains.clear();
	st.spaces.clear();
	st.events.clear();
	st.session = st.running = st.exitRequested = st.attached = st.syncFocused = st.profileBound = false;
	st.sessionState = XR_SESSION_STATE_UNKNOWN;
	st.bindings.clear();
	st.inputs.clear();
	st.lastInputs.clear();
	st.nextFrame = {};
}

bool validSession(XrSession session) { return st.session && fromHandle(session) == 0; }
bool validInstance(XrInstance instance) { return st.instance && fromHandle(instance) == 0; }
Space *getSpace(XrSpace space)
{
	const size_t i = fromHandle(space);
	return i < st.spaces.size() && st.spaces[i].used ? &st.spaces[i] : nullptr;
}
Swapchain *getSwapchain(XrSwapchain swapchain)
{
	const size_t i = fromHandle(swapchain);
	return i < st.swapchains.size() && st.swapchains[i].used ? &st.swapchains[i] : nullptr;
}

// Which hand an action space follows: its subaction path, else its action's first binding.
int handOf(const Space& s)
{
	const std::string& sub = pathString(s.subaction);
	if (sub == "/user/hand/left")
		return LeftHand;
	if (sub == "/user/hand/right")
		return RightHand;
	for (const auto& [action, path] : st.bindings)
		if (action == s.action)
			return path.rfind("/user/hand/left/", 0) == 0 ? LeftHand : RightHand;
	return RightHand;
}

// A space's pose in the stage (LOCAL and STAGE are the same here). Hands: aim and grip alike.
Pose worldPose(const Space& s)
{
	if (s.action != None)
		return compose(livePose(handOf(s)), s.offset);
	return s.reference == XR_REFERENCE_SPACE_TYPE_VIEW ? compose(livePose(Head), s.offset) : s.offset;
}

constexpr XrSpaceLocationFlags Tracked = XR_SPACE_LOCATION_ORIENTATION_VALID_BIT | XR_SPACE_LOCATION_POSITION_VALID_BIT
		| XR_SPACE_LOCATION_ORIENTATION_TRACKED_BIT | XR_SPACE_LOCATION_POSITION_TRACKED_BIT;

// The input paths bound to an action, of one hand (its subaction path) or of any.
std::vector<const std::string *> boundTo(size_t action, XrPath subaction)
{
	std::vector<const std::string *> paths;
	const std::string prefix = subaction != XR_NULL_PATH ? pathString(subaction) + "/" : std::string();
	for (const auto& [a, path] : st.bindings)
		if (a == action && path.compare(0, prefix.size(), prefix) == 0)
			paths.push_back(&path);
	return paths;
}
float valueOf(const std::map<std::string, float>& inputs, const std::string& path)
{
	const auto it = inputs.find(path);
	return it == inputs.end() ? 0.f : it->second;
}
XrResult actionFor(const XrActionStateGetInfo *info, XrActionType type, size_t& action)
{
	if (!st.attached)
		return XR_ERROR_ACTIONSET_NOT_ATTACHED;
	if (info == nullptr)
		return XR_ERROR_VALIDATION_FAILURE;
	action = fromHandle(info->action);
	if (action >= st.actions.size() || !st.actions[action].used)
		return XR_ERROR_HANDLE_INVALID;
	return st.actions[action].type == type ? XR_SUCCESS : XR_ERROR_ACTION_TYPE_MISMATCH;
}

// Shows a swapchain's last image in the window: the headset's view on the PC.
void show(const XrSwapchainSubImage& sub)
{
	const Swapchain *c = getSwapchain(sub.swapchain);
	if (c == nullptr || c->released >= c->images.size())
		return;
	const int w = sub.imageRect.extent.width > 0 ? sub.imageRect.extent.width : c->width;
	const int h = sub.imageRect.extent.height > 0 ? sub.imageRect.extent.height : c->height;
	if (st.showFbo == 0)
		glGenFramebuffers(1, &st.showFbo);
	glBindFramebuffer(GL_READ_FRAMEBUFFER, st.showFbo);
	glFramebufferTexture2D(GL_READ_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, c->images[c->released], 0);
	glBindFramebuffer(GL_DRAW_FRAMEBUFFER, 0);
	glcache.Disable(GL_SCISSOR_TEST);
	glBlitFramebuffer(sub.imageRect.offset.x, sub.imageRect.offset.y, sub.imageRect.offset.x + w, sub.imageRect.offset.y + h,
			0, 0, settings.display.width, settings.display.height, GL_COLOR_BUFFER_BIT, GL_LINEAR);
	glBindFramebuffer(GL_FRAMEBUFFER, 0);
	if (imguiDriver != nullptr)
		imguiDriver->setFrameRendered();		// mainui presents it
}

//
// The OpenXR functions
//
#define SIM_FN(name) XrResult XRAPI_CALL sim##name

SIM_FN(GetInstanceProcAddr)(XrInstance, const char *name, PFN_xrVoidFunction *function)
{
	if (name == nullptr || function == nullptr)
		return XR_ERROR_VALIDATION_FAILURE;
	*function = proc(name);
	return *function != nullptr ? XR_SUCCESS : XR_ERROR_FUNCTION_UNSUPPORTED;
}

const struct { const char *name; uint32_t version; } Extensions[] {
	{ XR_KHR_OPENGL_ENABLE_EXTENSION_NAME, XR_KHR_opengl_enable_SPEC_VERSION },
	{ XR_FB_DISPLAY_REFRESH_RATE_EXTENSION_NAME, XR_FB_display_refresh_rate_SPEC_VERSION },
	// the last one: only with vr.XrSimRenderModel
	{ XR_FB_RENDER_MODEL_EXTENSION_NAME, XR_FB_render_model_SPEC_VERSION },
};
// XR_FB_render_model: listed (and its functions offered) only with a GLB file to serve as both
// controllers' model, so without it the app finds none, as on a PC VR runtime
bool renderModels() { return !config::VrXrSimRenderModel.get().empty(); }
uint32_t extensionCount() { return (uint32_t)std::size(Extensions) - (renderModels() ? 0 : 1); }

SIM_FN(EnumerateInstanceExtensionProperties)(const char *layerName, uint32_t capacity, uint32_t *count, XrExtensionProperties *properties)
{
	if (layerName != nullptr)
		return XR_ERROR_API_LAYER_NOT_PRESENT;
	return fill(capacity, count, properties, extensionCount(), [](XrExtensionProperties& p, uint32_t i) {
		strncpy(p.extensionName, Extensions[i].name, XR_MAX_EXTENSION_NAME_SIZE - 1);
		p.extensionVersion = Extensions[i].version;
	});
}

SIM_FN(CreateInstance)(const XrInstanceCreateInfo *info, XrInstance *instance)
{
	if (info == nullptr || instance == nullptr)
		return XR_ERROR_VALIDATION_FAILURE;
	for (uint32_t i = 0; i < info->enabledExtensionCount; i++)
	{
		bool known = false;
		for (uint32_t e = 0; e < extensionCount(); e++)
			known = known || !strcmp(Extensions[e].name, info->enabledExtensionNames[i]);
		if (!known)
		{
			ERROR_LOG(RENDERER, "XRSIM: no extension %s", info->enabledExtensionNames[i]);
			return XR_ERROR_EXTENSION_NOT_PRESENT;
		}
	}
	deleteSession();
	st = State();
	st.instance = true;
	// XR_FB_render_model's functions only for an instance that enabled it, as a loader hands them out
	for (uint32_t i = 0; i < info->enabledExtensionCount; i++)
		st.renderModelEnabled = st.renderModelEnabled || !strcmp(info->enabledExtensionNames[i], XR_FB_RENDER_MODEL_EXTENSION_NAME);
	*instance = toHandle<XrInstance>(0);
	NOTICE_LOG(RENDERER, "XRSIM: runtime up for \"%s\" (%dx%d per eye, %d Hz, %s)", info->applicationInfo.applicationName,
			EyeWidth, EyeHeight, RefreshRate, profilePath().c_str());
	return XR_SUCCESS;
}

SIM_FN(DestroyInstance)(XrInstance instance)
{
	if (!validInstance(instance))
		return XR_ERROR_HANDLE_INVALID;
	deleteSession();
	st = State();
	return XR_SUCCESS;
}

SIM_FN(GetInstanceProperties)(XrInstance instance, XrInstanceProperties *properties)
{
	if (!validInstance(instance))
		return XR_ERROR_HANDLE_INVALID;
	properties->runtimeVersion = XR_MAKE_VERSION(1, 0, 0);
	strncpy(properties->runtimeName, RuntimeName, XR_MAX_RUNTIME_NAME_SIZE - 1);
	return XR_SUCCESS;
}

SIM_FN(PollEvent)(XrInstance instance, XrEventDataBuffer *event)
{
	if (!validInstance(instance))
		return XR_ERROR_HANDLE_INVALID;
	followFocus();
	if (st.events.empty())
		return XR_EVENT_UNAVAILABLE;
	*event = st.events.front();
	st.events.pop_front();
	return XR_SUCCESS;
}

SIM_FN(ResultToString)(XrInstance, XrResult value, char buffer[XR_MAX_RESULT_STRING_SIZE])
{
	const char *name = nullptr;
	switch (value)
	{
#define NAME(r) case r: name = #r; break;
	NAME(XR_SUCCESS) NAME(XR_TIMEOUT_EXPIRED) NAME(XR_SESSION_NOT_FOCUSED) NAME(XR_EVENT_UNAVAILABLE)
	NAME(XR_SPACE_BOUNDS_UNAVAILABLE) NAME(XR_ERROR_VALIDATION_FAILURE) NAME(XR_ERROR_HANDLE_INVALID)
	NAME(XR_ERROR_FUNCTION_UNSUPPORTED) NAME(XR_ERROR_EXTENSION_NOT_PRESENT) NAME(XR_ERROR_API_LAYER_NOT_PRESENT)
	NAME(XR_ERROR_SIZE_INSUFFICIENT) NAME(XR_ERROR_LIMIT_REACHED) NAME(XR_ERROR_FORM_FACTOR_UNSUPPORTED)
	NAME(XR_ERROR_SESSION_NOT_READY) NAME(XR_ERROR_SESSION_NOT_STOPPING) NAME(XR_ERROR_SESSION_NOT_RUNNING)
	NAME(XR_ERROR_SESSION_RUNNING) NAME(XR_ERROR_SWAPCHAIN_FORMAT_UNSUPPORTED) NAME(XR_ERROR_PATH_FORMAT_INVALID)
	NAME(XR_ERROR_PATH_INVALID) NAME(XR_ERROR_ACTIONSET_NOT_ATTACHED) NAME(XR_ERROR_ACTIONSETS_ALREADY_ATTACHED)
	NAME(XR_ERROR_ACTION_TYPE_MISMATCH) NAME(XR_ERROR_GRAPHICS_DEVICE_INVALID)
	NAME(XR_ERROR_GRAPHICS_REQUIREMENTS_CALL_MISSING) NAME(XR_ERROR_VIEW_CONFIGURATION_TYPE_UNSUPPORTED)
	NAME(XR_ERROR_REFERENCE_SPACE_UNSUPPORTED) NAME(XR_ERROR_CALL_ORDER_INVALID) NAME(XR_ERROR_PATH_UNSUPPORTED)
	NAME(XR_ERROR_LAYER_INVALID)
#undef NAME
	default:
		break;
	}
	if (name != nullptr)
		snprintf(buffer, XR_MAX_RESULT_STRING_SIZE, "%s", name);
	else
		snprintf(buffer, XR_MAX_RESULT_STRING_SIZE, value >= 0 ? "XR_UNKNOWN_SUCCESS_%d" : "XR_UNKNOWN_FAILURE_%d", (int)value);
	return XR_SUCCESS;
}

SIM_FN(StringToPath)(XrInstance instance, const char *s, XrPath *path)
{
	if (!validInstance(instance))
		return XR_ERROR_HANDLE_INVALID;
	const size_t n = s != nullptr ? strlen(s) : 0;
	if (n < 2 || s[0] != '/' || s[n - 1] == '/' || n >= XR_MAX_PATH_LENGTH)
		return XR_ERROR_PATH_FORMAT_INVALID;
	*path = toPath(s);
	return XR_SUCCESS;
}

SIM_FN(PathToString)(XrInstance instance, XrPath path, uint32_t capacity, uint32_t *count, char *buffer)
{
	if (!validInstance(instance))
		return XR_ERROR_HANDLE_INVALID;
	const std::string& s = pathString(path);
	if (s.empty())
		return XR_ERROR_PATH_INVALID;
	return fill(capacity, count, buffer, (uint32_t)s.size() + 1, [&s](char& c, uint32_t i) { c = s.c_str()[i]; });
}

SIM_FN(GetSystem)(XrInstance instance, const XrSystemGetInfo *info, XrSystemId *systemId)
{
	if (!validInstance(instance))
		return XR_ERROR_HANDLE_INVALID;
	if (info->formFactor != XR_FORM_FACTOR_HEAD_MOUNTED_DISPLAY)
		return XR_ERROR_FORM_FACTOR_UNSUPPORTED;
	*systemId = 1;
	return XR_SUCCESS;
}

SIM_FN(GetSystemProperties)(XrInstance instance, XrSystemId, XrSystemProperties *properties)
{
	if (!validInstance(instance))
		return XR_ERROR_HANDLE_INVALID;
	properties->systemId = 1;
	properties->vendorId = 0;
	strncpy(properties->systemName, RuntimeName, XR_MAX_SYSTEM_NAME_SIZE - 1);
	properties->graphicsProperties = { 4096, 4096, 16 };
	properties->trackingProperties = { XR_TRUE, XR_TRUE };
	return XR_SUCCESS;
}

SIM_FN(EnumerateEnvironmentBlendModes)(XrInstance instance, XrSystemId, XrViewConfigurationType, uint32_t capacity,
		uint32_t *count, XrEnvironmentBlendMode *modes)
{
	if (!validInstance(instance))
		return XR_ERROR_HANDLE_INVALID;
	return fill(capacity, count, modes, 1, [](XrEnvironmentBlendMode& m, uint32_t) { m = XR_ENVIRONMENT_BLEND_MODE_OPAQUE; });
}

SIM_FN(EnumerateViewConfigurations)(XrInstance instance, XrSystemId, uint32_t capacity, uint32_t *count, XrViewConfigurationType *types)
{
	if (!validInstance(instance))
		return XR_ERROR_HANDLE_INVALID;
	return fill(capacity, count, types, 1, [](XrViewConfigurationType& t, uint32_t) { t = XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO; });
}

SIM_FN(GetViewConfigurationProperties)(XrInstance instance, XrSystemId, XrViewConfigurationType type, XrViewConfigurationProperties *properties)
{
	if (!validInstance(instance))
		return XR_ERROR_HANDLE_INVALID;
	if (type != XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO)
		return XR_ERROR_VIEW_CONFIGURATION_TYPE_UNSUPPORTED;
	properties->viewConfigurationType = type;
	properties->fovMutable = XR_FALSE;
	return XR_SUCCESS;
}

SIM_FN(EnumerateViewConfigurationViews)(XrInstance instance, XrSystemId, XrViewConfigurationType type, uint32_t capacity,
		uint32_t *count, XrViewConfigurationView *views)
{
	if (!validInstance(instance))
		return XR_ERROR_HANDLE_INVALID;
	if (type != XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO)
		return XR_ERROR_VIEW_CONFIGURATION_TYPE_UNSUPPORTED;
	return fill(capacity, count, views, 2, [](XrViewConfigurationView& v, uint32_t) {
		v.recommendedImageRectWidth = EyeWidth;
		v.maxImageRectWidth = 4096;
		v.recommendedImageRectHeight = EyeHeight;
		v.maxImageRectHeight = 4096;
		v.recommendedSwapchainSampleCount = 1;
		v.maxSwapchainSampleCount = 1;
	});
}

SIM_FN(GetOpenGLGraphicsRequirementsKHR)(XrInstance instance, XrSystemId, XrGraphicsRequirementsOpenGLKHR *requirements)
{
	if (!validInstance(instance))
		return XR_ERROR_HANDLE_INVALID;
	requirements->minApiVersionSupported = XR_MAKE_VERSION(3, 0, 0);
	requirements->maxApiVersionSupported = XR_MAKE_VERSION(4, 6, 0);
	st.requirementsAsked = true;
	return XR_SUCCESS;
}

SIM_FN(CreateSession)(XrInstance instance, const XrSessionCreateInfo *info, XrSession *session)
{
	if (!validInstance(instance))
		return XR_ERROR_HANDLE_INVALID;
	if (st.session)
		return XR_ERROR_LIMIT_REACHED;
	if (!st.requirementsAsked)
		return XR_ERROR_GRAPHICS_REQUIREMENTS_CALL_MISSING;
	const XrBaseInStructure *next = (const XrBaseInStructure *)info->next;
	while (next != nullptr && next->type != XR_TYPE_GRAPHICS_BINDING_OPENGL_WIN32_KHR)
		next = next->next;
	if (next == nullptr || ((const XrGraphicsBindingOpenGLWin32KHR *)next)->hGLRC == nullptr)
		return XR_ERROR_GRAPHICS_DEVICE_INVALID;
	st.session = true;
	*session = toHandle<XrSession>(0);
	setState(XR_SESSION_STATE_IDLE);
	setState(XR_SESSION_STATE_READY);
	return XR_SUCCESS;
}

SIM_FN(DestroySession)(XrSession session)
{
	if (!validSession(session))
		return XR_ERROR_HANDLE_INVALID;
	deleteSession();
	return XR_SUCCESS;
}

SIM_FN(BeginSession)(XrSession session, const XrSessionBeginInfo *info)
{
	if (!validSession(session))
		return XR_ERROR_HANDLE_INVALID;
	if (st.running)
		return XR_ERROR_SESSION_RUNNING;
	if (st.sessionState != XR_SESSION_STATE_READY)
		return XR_ERROR_SESSION_NOT_READY;
	if (info->primaryViewConfigurationType != XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO)
		return XR_ERROR_VIEW_CONFIGURATION_TYPE_UNSUPPORTED;
	st.running = true;
	setState(XR_SESSION_STATE_SYNCHRONIZED);
	setState(XR_SESSION_STATE_VISIBLE);
	if (wantFocus)
		setState(XR_SESSION_STATE_FOCUSED);
	return XR_SUCCESS;
}

SIM_FN(EndSession)(XrSession session)
{
	if (!validSession(session))
		return XR_ERROR_HANDLE_INVALID;
	if (!st.running)
		return XR_ERROR_SESSION_NOT_RUNNING;
	if (st.sessionState != XR_SESSION_STATE_STOPPING)
		return XR_ERROR_SESSION_NOT_STOPPING;
	st.running = false;
	setState(XR_SESSION_STATE_IDLE);
	if (st.exitRequested)
		setState(XR_SESSION_STATE_EXITING);
	return XR_SUCCESS;
}

SIM_FN(RequestExitSession)(XrSession session)
{
	if (!validSession(session))
		return XR_ERROR_HANDLE_INVALID;
	if (!st.running)
		return XR_ERROR_SESSION_NOT_RUNNING;
	st.exitRequested = true;
	if (st.sessionState == XR_SESSION_STATE_FOCUSED)
		setState(XR_SESSION_STATE_VISIBLE);
	if (st.sessionState == XR_SESSION_STATE_VISIBLE)
		setState(XR_SESSION_STATE_SYNCHRONIZED);
	setState(XR_SESSION_STATE_STOPPING);
	return XR_SUCCESS;
}

SIM_FN(EnumerateReferenceSpaces)(XrSession session, uint32_t capacity, uint32_t *count, XrReferenceSpaceType *spaces)
{
	if (!validSession(session))
		return XR_ERROR_HANDLE_INVALID;
	static const XrReferenceSpaceType types[] { XR_REFERENCE_SPACE_TYPE_VIEW, XR_REFERENCE_SPACE_TYPE_LOCAL, XR_REFERENCE_SPACE_TYPE_STAGE };
	return fill(capacity, count, spaces, 3, [](XrReferenceSpaceType& t, uint32_t i) { t = types[i]; });
}

SIM_FN(CreateReferenceSpace)(XrSession session, const XrReferenceSpaceCreateInfo *info, XrSpace *space)
{
	if (!validSession(session))
		return XR_ERROR_HANDLE_INVALID;
	if (info->referenceSpaceType != XR_REFERENCE_SPACE_TYPE_VIEW && info->referenceSpaceType != XR_REFERENCE_SPACE_TYPE_LOCAL
			&& info->referenceSpaceType != XR_REFERENCE_SPACE_TYPE_STAGE)
		return XR_ERROR_REFERENCE_SPACE_UNSUPPORTED;
	Space s;
	s.reference = info->referenceSpaceType;
	s.offset = fromXr(info->poseInReferenceSpace);
	st.spaces.push_back(s);
	*space = toHandle<XrSpace>(st.spaces.size() - 1);
	return XR_SUCCESS;
}

SIM_FN(GetReferenceSpaceBoundsRect)(XrSession session, XrReferenceSpaceType type, XrExtent2Df *bounds)
{
	if (!validSession(session))
		return XR_ERROR_HANDLE_INVALID;
	if (type != XR_REFERENCE_SPACE_TYPE_STAGE)
	{
		*bounds = { 0.f, 0.f };
		return XR_SPACE_BOUNDS_UNAVAILABLE;
	}
	*bounds = { 2.f, 2.f };
	return XR_SUCCESS;
}

SIM_FN(CreateActionSpace)(XrSession session, const XrActionSpaceCreateInfo *info, XrSpace *space)
{
	if (!validSession(session))
		return XR_ERROR_HANDLE_INVALID;
	const size_t action = fromHandle(info->action);
	if (action >= st.actions.size() || !st.actions[action].used)
		return XR_ERROR_HANDLE_INVALID;
	if (st.actions[action].type != XR_ACTION_TYPE_POSE_INPUT)
		return XR_ERROR_ACTION_TYPE_MISMATCH;
	Space s;
	s.action = action;
	s.subaction = info->subactionPath;
	s.offset = fromXr(info->poseInActionSpace);
	st.spaces.push_back(s);
	*space = toHandle<XrSpace>(st.spaces.size() - 1);
	return XR_SUCCESS;
}

SIM_FN(DestroySpace)(XrSpace space)
{
	Space *s = getSpace(space);
	if (s == nullptr)
		return XR_ERROR_HANDLE_INVALID;
	s->used = false;
	return XR_SUCCESS;
}

SIM_FN(LocateSpace)(XrSpace space, XrSpace baseSpace, XrTime, XrSpaceLocation *location)
{
	const Space *s = getSpace(space), *base = getSpace(baseSpace);
	if (s == nullptr || base == nullptr)
		return XR_ERROR_HANDLE_INVALID;
	location->locationFlags = Tracked;
	location->pose = toXr(relative(worldPose(*base), worldPose(*s)));
	return XR_SUCCESS;
}

SIM_FN(LocateViews)(XrSession session, const XrViewLocateInfo *info, XrViewState *viewState, uint32_t capacity, uint32_t *count, XrView *views)
{
	if (!validSession(session))
		return XR_ERROR_HANDLE_INVALID;
	if (info->viewConfigurationType != XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO)
		return XR_ERROR_VIEW_CONFIGURATION_TYPE_UNSUPPORTED;
	const Space *base = getSpace(info->space);
	if (base == nullptr)
		return XR_ERROR_HANDLE_INVALID;
	const Pose head = livePose(Head);
	const Pose from = worldPose(*base);
	viewState->viewStateFlags = wantHeadTracked ? XR_VIEW_STATE_ORIENTATION_VALID_BIT | XR_VIEW_STATE_POSITION_VALID_BIT
			| XR_VIEW_STATE_ORIENTATION_TRACKED_BIT | XR_VIEW_STATE_POSITION_TRACKED_BIT : 0;
	return fill(capacity, count, views, 2, [&](XrView& v, uint32_t i) {
		Pose eye;
		eye.position.x = i == 0 ? -HalfIpd : HalfIpd;
		v.pose = toXr(relative(from, compose(head, eye)));
		v.fov.angleLeft = -FovAcross;
		v.fov.angleRight = FovAcross;
		v.fov.angleUp = FovUpDown;
		v.fov.angleDown = -FovUpDown;
	});
}

SIM_FN(EnumerateSwapchainFormats)(XrSession session, uint32_t capacity, uint32_t *count, int64_t *formats)
{
	if (!validSession(session))
		return XR_ERROR_HANDLE_INVALID;
	return fill(capacity, count, formats, 1, [](int64_t& f, uint32_t) { f = GL_RGBA8; });
}

SIM_FN(CreateSwapchain)(XrSession session, const XrSwapchainCreateInfo *info, XrSwapchain *swapchain)
{
	if (!validSession(session))
		return XR_ERROR_HANDLE_INVALID;
	if (info->format != GL_RGBA8)
		return XR_ERROR_SWAPCHAIN_FORMAT_UNSUPPORTED;
	if (info->width == 0 || info->height == 0 || info->width > 4096 || info->height > 4096 || info->faceCount != 1 || info->arraySize != 1)
		return XR_ERROR_VALIDATION_FAILURE;
	Swapchain c;
	c.width = (int)info->width;
	c.height = (int)info->height;
	c.images.resize(SwapchainLength);
	glGenTextures((GLsizei)c.images.size(), c.images.data());
	for (GLuint t : c.images)
	{
		glcache.BindTexture(GL_TEXTURE_2D, t);
		glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, c.width, c.height, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
		glcache.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
		glcache.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
		glcache.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
		glcache.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
	}
	glcache.BindTexture(GL_TEXTURE_2D, 0);
	st.swapchains.push_back(c);
	*swapchain = toHandle<XrSwapchain>(st.swapchains.size() - 1);
	return XR_SUCCESS;
}

SIM_FN(DestroySwapchain)(XrSwapchain swapchain)
{
	Swapchain *c = getSwapchain(swapchain);
	if (c == nullptr)
		return XR_ERROR_HANDLE_INVALID;
	glcache.DeleteTextures((GLsizei)c->images.size(), c->images.data());
	c->images.clear();
	c->used = false;
	return XR_SUCCESS;
}

SIM_FN(EnumerateSwapchainImages)(XrSwapchain swapchain, uint32_t capacity, uint32_t *count, XrSwapchainImageBaseHeader *images)
{
	const Swapchain *c = getSwapchain(swapchain);
	if (c == nullptr)
		return XR_ERROR_HANDLE_INVALID;
	if (capacity != 0 && images != nullptr && images->type != XR_TYPE_SWAPCHAIN_IMAGE_OPENGL_KHR)
		return XR_ERROR_VALIDATION_FAILURE;
	return fill(capacity, count, (XrSwapchainImageOpenGLKHR *)images, (uint32_t)c->images.size(),
			[c](XrSwapchainImageOpenGLKHR& image, uint32_t i) { image.image = c->images[i]; });
}

SIM_FN(AcquireSwapchainImage)(XrSwapchain swapchain, const XrSwapchainImageAcquireInfo *, uint32_t *index)
{
	Swapchain *c = getSwapchain(swapchain);
	if (c == nullptr)
		return XR_ERROR_HANDLE_INVALID;
	*index = c->acquired = c->next;
	c->next = (c->next + 1) % (u32)c->images.size();
	return XR_SUCCESS;
}

SIM_FN(WaitSwapchainImage)(XrSwapchain swapchain, const XrSwapchainImageWaitInfo *)
{
	return getSwapchain(swapchain) != nullptr ? XR_SUCCESS : XR_ERROR_HANDLE_INVALID;
}

SIM_FN(ReleaseSwapchainImage)(XrSwapchain swapchain, const XrSwapchainImageReleaseInfo *)
{
	Swapchain *c = getSwapchain(swapchain);
	if (c == nullptr)
		return XR_ERROR_HANDLE_INVALID;
	c->released = c->acquired;
	return XR_SUCCESS;
}

// Holds the render loop to 72 Hz, like a headset's compositor.
SIM_FN(WaitFrame)(XrSession session, const XrFrameWaitInfo *, XrFrameState *state)
{
	if (!validSession(session))
		return XR_ERROR_HANDLE_INVALID;
	if (!st.running)
		return XR_ERROR_SESSION_NOT_RUNNING;
	using clock = std::chrono::steady_clock;
	const auto period = std::chrono::nanoseconds(1000000000 / RefreshRate);
	const auto now = clock::now();
	if (st.nextFrame == clock::time_point() || now - st.nextFrame > period)
		st.nextFrame = now;		// the first frame, or one well late: no burst to catch up
	else
		std::this_thread::sleep_until(st.nextFrame);
	state->predictedDisplayPeriod = period.count();
	state->predictedDisplayTime = xrTime(st.nextFrame + period);
	state->shouldRender = st.sessionState == XR_SESSION_STATE_VISIBLE || st.sessionState == XR_SESSION_STATE_FOCUSED;
	st.nextFrame += period;
	return XR_SUCCESS;
}

SIM_FN(BeginFrame)(XrSession session, const XrFrameBeginInfo *)
{
	if (!validSession(session))
		return XR_ERROR_HANDLE_INVALID;
	return st.running ? XR_SUCCESS : XR_ERROR_SESSION_NOT_RUNNING;
}

// The left eye of the first projection layer goes to the window.
SIM_FN(EndFrame)(XrSession session, const XrFrameEndInfo *info)
{
	if (!validSession(session))
		return XR_ERROR_HANDLE_INVALID;
	if (!st.running)
		return XR_ERROR_SESSION_NOT_RUNNING;
	for (uint32_t i = 0; i < info->layerCount; i++)
		if (info->layers[i] != nullptr && info->layers[i]->type == XR_TYPE_COMPOSITION_LAYER_PROJECTION)
		{
			const auto *layer = (const XrCompositionLayerProjection *)info->layers[i];
			if (layer->viewCount < 1 || layer->views == nullptr)
				return XR_ERROR_LAYER_INVALID;
			show(layer->views[0].subImage);
			break;
		}
	return XR_SUCCESS;
}

SIM_FN(CreateActionSet)(XrInstance instance, const XrActionSetCreateInfo *info, XrActionSet *actionSet)
{
	if (!validInstance(instance))
		return XR_ERROR_HANDLE_INVALID;
	ActionSet set;
	set.name = info->actionSetName;
	st.actionSets.push_back(set);
	*actionSet = toHandle<XrActionSet>(st.actionSets.size() - 1);
	return XR_SUCCESS;
}

SIM_FN(DestroyActionSet)(XrActionSet actionSet)
{
	const size_t i = fromHandle(actionSet);
	if (i >= st.actionSets.size() || !st.actionSets[i].used)
		return XR_ERROR_HANDLE_INVALID;
	st.actionSets[i].used = false;
	return XR_SUCCESS;
}

SIM_FN(CreateAction)(XrActionSet actionSet, const XrActionCreateInfo *info, XrAction *action)
{
	const size_t set = fromHandle(actionSet);
	if (set >= st.actionSets.size() || !st.actionSets[set].used)
		return XR_ERROR_HANDLE_INVALID;
	if (st.attached)
		return XR_ERROR_ACTIONSETS_ALREADY_ATTACHED;
	Action a;
	a.set = set;
	a.name = info->actionName;
	a.type = info->actionType;
	st.actions.push_back(a);
	*action = toHandle<XrAction>(st.actions.size() - 1);
	return XR_SUCCESS;
}

SIM_FN(DestroyAction)(XrAction action)
{
	const size_t i = fromHandle(action);
	if (i >= st.actions.size() || !st.actions[i].used)
		return XR_ERROR_HANDLE_INVALID;
	st.actions[i].used = false;
	return XR_SUCCESS;
}

SIM_FN(SuggestInteractionProfileBindings)(XrInstance instance, const XrInteractionProfileSuggestedBinding *suggested)
{
	if (!validInstance(instance))
		return XR_ERROR_HANDLE_INVALID;
	if (st.attached)
		return XR_ERROR_ACTIONSETS_ALREADY_ATTACHED;
	if (pathString(suggested->interactionProfile).rfind("/interaction_profiles/", 0) != 0)
		return XR_ERROR_PATH_UNSUPPORTED;
	std::vector<XrActionSuggestedBinding> bindings;
	for (uint32_t i = 0; i < suggested->countSuggestedBindings; i++)
	{
		const XrActionSuggestedBinding& b = suggested->suggestedBindings[i];
		const size_t action = fromHandle(b.action);
		if (action >= st.actions.size() || !st.actions[action].used)
			return XR_ERROR_HANDLE_INVALID;
		if (pathString(b.binding).rfind("/user/", 0) != 0)
			return XR_ERROR_PATH_UNSUPPORTED;
		bindings.push_back(b);
	}
	st.suggested[suggested->interactionProfile] = bindings;
	return XR_SUCCESS;
}

SIM_FN(AttachSessionActionSets)(XrSession session, const XrSessionActionSetsAttachInfo *)
{
	if (!validSession(session))
		return XR_ERROR_HANDLE_INVALID;
	if (st.attached)
		return XR_ERROR_ACTIONSETS_ALREADY_ATTACHED;
	st.attached = true;
	st.bindings.clear();
	const std::string profile = profilePath();
	const auto it = st.suggested.find(toPath(profile));
	if (it != st.suggested.end())
		for (const XrActionSuggestedBinding& b : it->second)
			st.bindings.emplace_back(fromHandle(b.action), pathString(b.binding));
	if (st.bindings.empty())
		WARN_LOG(RENDERER, "XRSIM: no bindings suggested for %s: no input", profile.c_str());
	else
		NOTICE_LOG(RENDERER, "XRSIM: %d bindings for %s", (int)st.bindings.size(), profile.c_str());
	// (the profile is bound at the first xrSyncActions while focused, not here)
	return XR_SUCCESS;
}

SIM_FN(GetCurrentInteractionProfile)(XrSession session, XrPath topLevelUserPath, XrInteractionProfileState *state)
{
	if (!validSession(session))
		return XR_ERROR_HANDLE_INVALID;
	if (!st.attached)
		return XR_ERROR_ACTIONSET_NOT_ATTACHED;
	const std::string& user = pathString(topLevelUserPath);
	if (user.empty())
		return XR_ERROR_PATH_INVALID;
	state->interactionProfile = st.profileBound && (user == "/user/hand/left" || user == "/user/hand/right")
			? toPath(profilePath()) : XR_NULL_PATH;
	return XR_SUCCESS;
}

// The inputs as the harness last set them. Out of focus (XRSTATE visible), as a runtime does:
// XR_SESSION_NOT_FOCUSED, and every action inactive.
SIM_FN(SyncActions)(XrSession session, const XrActionsSyncInfo *)
{
	if (!validSession(session))
		return XR_ERROR_HANDLE_INVALID;
	if (!st.attached)
		return XR_ERROR_ACTIONSET_NOT_ATTACHED;
	if (!st.running)
		return XR_ERROR_SESSION_NOT_RUNNING;
	st.lastInputs = std::move(st.inputs);
	{
		std::lock_guard<std::mutex> lock(inputMutex);
		st.inputs = liveInputs;
	}
	st.syncTime = xrTime(std::chrono::steady_clock::now());
	st.syncFocused = st.sessionState == XR_SESSION_STATE_FOCUSED;
	if (st.syncFocused && !st.profileBound)
	{
		// the interaction profile bound now, as a runtime binds it
		st.profileBound = true;
		XrEventDataInteractionProfileChanged event { XR_TYPE_EVENT_DATA_INTERACTION_PROFILE_CHANGED };
		event.session = session;
		pushEvent(&event, sizeof(event));
		NOTICE_LOG(RENDERER, "XRSIM: interaction profile bound (%s)", profilePath().c_str());
	}
	return st.syncFocused ? XR_SUCCESS : XR_SESSION_NOT_FOCUSED;
}

// on at 0.5 or more
SIM_FN(GetActionStateBoolean)(XrSession session, const XrActionStateGetInfo *info, XrActionStateBoolean *state)
{
	size_t action;
	if (!validSession(session))
		return XR_ERROR_HANDLE_INVALID;
	if (XrResult r = actionFor(info, XR_ACTION_TYPE_BOOLEAN_INPUT, action); XR_FAILED(r))
		return r;
	bool now = false, before = false;
	for (const std::string *path : boundTo(action, info->subactionPath))
	{
		now = now || valueOf(st.inputs, *path) >= 0.5f;
		before = before || valueOf(st.lastInputs, *path) >= 0.5f;
	}
	state->isActive = st.syncFocused;
	state->currentState = st.syncFocused && now;
	state->changedSinceLastSync = st.syncFocused && now != before;
	state->lastChangeTime = state->changedSinceLastSync ? st.syncTime : 0;
	return XR_SUCCESS;
}

// the largest of the bound inputs
SIM_FN(GetActionStateFloat)(XrSession session, const XrActionStateGetInfo *info, XrActionStateFloat *state)
{
	size_t action;
	if (!validSession(session))
		return XR_ERROR_HANDLE_INVALID;
	if (XrResult r = actionFor(info, XR_ACTION_TYPE_FLOAT_INPUT, action); XR_FAILED(r))
		return r;
	float now = 0.f, before = 0.f;
	for (const std::string *path : boundTo(action, info->subactionPath))
	{
		const float v = valueOf(st.inputs, *path), w = valueOf(st.lastInputs, *path);
		if (std::abs(v) > std::abs(now))
			now = v;
		if (std::abs(w) > std::abs(before))
			before = w;
	}
	state->isActive = st.syncFocused;
	state->currentState = st.syncFocused ? now : 0.f;
	state->changedSinceLastSync = st.syncFocused && now != before;
	state->lastChangeTime = state->changedSinceLastSync ? st.syncTime : 0;
	return XR_SUCCESS;
}

// a stick or pad (<path>/x and <path>/y): the one pushed furthest
SIM_FN(GetActionStateVector2f)(XrSession session, const XrActionStateGetInfo *info, XrActionStateVector2f *state)
{
	size_t action;
	if (!validSession(session))
		return XR_ERROR_HANDLE_INVALID;
	if (XrResult r = actionFor(info, XR_ACTION_TYPE_VECTOR2F_INPUT, action); XR_FAILED(r))
		return r;
	glm::vec2 now(0.f), before(0.f);
	for (const std::string *path : boundTo(action, info->subactionPath))
	{
		const glm::vec2 v(valueOf(st.inputs, *path + "/x"), valueOf(st.inputs, *path + "/y"));
		const glm::vec2 w(valueOf(st.lastInputs, *path + "/x"), valueOf(st.lastInputs, *path + "/y"));
		if (glm::length(v) > glm::length(now))
			now = v;
		if (glm::length(w) > glm::length(before))
			before = w;
	}
	if (!st.syncFocused)
		now = glm::vec2(0.f);
	state->isActive = st.syncFocused;
	state->currentState = { now.x, now.y };
	state->changedSinceLastSync = st.syncFocused && now != before;
	state->lastChangeTime = state->changedSinceLastSync ? st.syncTime : 0;
	return XR_SUCCESS;
}

SIM_FN(GetActionStatePose)(XrSession session, const XrActionStateGetInfo *info, XrActionStatePose *state)
{
	size_t action;
	if (!validSession(session))
		return XR_ERROR_HANDLE_INVALID;
	if (XrResult r = actionFor(info, XR_ACTION_TYPE_POSE_INPUT, action); XR_FAILED(r))
		return r;
	state->isActive = st.syncFocused;
	return XR_SUCCESS;
}

SIM_FN(ApplyHapticFeedback)(XrSession session, const XrHapticActionInfo *info, const XrHapticBaseHeader *feedback)
{
	if (!validSession(session))
		return XR_ERROR_HANDLE_INVALID;
	if (!st.attached)
		return XR_ERROR_ACTIONSET_NOT_ATTACHED;
	float seconds = 0.f, amplitude = 0.f;
	if (feedback != nullptr && feedback->type == XR_TYPE_HAPTIC_VIBRATION)
	{
		const auto *vibration = (const XrHapticVibration *)feedback;
		seconds = vibration->duration > 0 ? (float)vibration->duration / 1e9f : 0.f;	// (XR_MIN_HAPTIC_DURATION: -1)
		amplitude = vibration->amplitude;
	}
	NOTICE_LOG(INPUT, "XRSIM: haptic %s %.3f %.3f", pathString(info->subactionPath) == "/user/hand/left" ? "left" : "right",
			seconds, amplitude);
	return XR_SUCCESS;
}

SIM_FN(StopHapticFeedback)(XrSession session, const XrHapticActionInfo *)
{
	return validSession(session) ? XR_SUCCESS : XR_ERROR_HANDLE_INVALID;
}

SIM_FN(EnumerateDisplayRefreshRatesFB)(XrSession session, uint32_t capacity, uint32_t *count, float *rates)
{
	if (!validSession(session))
		return XR_ERROR_HANDLE_INVALID;
	return fill(capacity, count, rates, 1, [](float& r, uint32_t) { r = (float)RefreshRate; });
}

SIM_FN(GetDisplayRefreshRateFB)(XrSession session, float *rate)
{
	if (!validSession(session))
		return XR_ERROR_HANDLE_INVALID;
	*rate = (float)RefreshRate;
	return XR_SUCCESS;
}

SIM_FN(RequestDisplayRefreshRateFB)(XrSession session, float rate)
{
	if (!validSession(session))
		return XR_ERROR_HANDLE_INVALID;
	if (rate != 0.f && (int)rate != RefreshRate)
		NOTICE_LOG(RENDERER, "XRSIM: %d Hz asked for, staying at %d Hz", (int)rate, RefreshRate);
	return XR_SUCCESS;
}

// XR_FB_render_model (vr.XrSimRenderModel): that GLB file as both controllers' model, handed out
// as a Quest runtime hands out its controllers' (xr_controllers.h): its functions only to an
// instance that enabled the extension, an XrRenderModelCapabilitiesRequestFB wanted, and no
// model (XR_RENDER_MODEL_UNAVAILABLE_FB) until the interaction profile is bound
const char *const RenderModelPaths[] { "/model_fb/controller/left", "/model_fb/controller/right" };

SIM_FN(EnumerateRenderModelPathsFB)(XrSession session, uint32_t capacity, uint32_t *count, XrRenderModelPathInfoFB *paths)
{
	if (!validSession(session))
		return XR_ERROR_HANDLE_INVALID;
	return fill(capacity, count, paths, (uint32_t)std::size(RenderModelPaths), [](XrRenderModelPathInfoFB& p, uint32_t i) {
		p.path = toPath(RenderModelPaths[i]);
	});
}

SIM_FN(GetRenderModelPropertiesFB)(XrSession session, XrPath path, XrRenderModelPropertiesFB *properties)
{
	if (!validSession(session))
		return XR_ERROR_HANDLE_INVALID;
	if (properties == nullptr || properties->type != XR_TYPE_RENDER_MODEL_PROPERTIES_FB)
		return XR_ERROR_VALIDATION_FAILURE;
	bool request = false;
	for (const XrBaseInStructure *s = (const XrBaseInStructure *)properties->next; s != nullptr; s = s->next)
		request = request || s->type == XR_TYPE_RENDER_MODEL_CAPABILITIES_REQUEST_FB;
	if (!request)
	{
		ERROR_LOG(RENDERER, "XRSIM: xrGetRenderModelPropertiesFB with no XrRenderModelCapabilitiesRequestFB chained");
		return XR_ERROR_VALIDATION_FAILURE;
	}
	for (uint32_t i = 0; i < std::size(RenderModelPaths); i++)
		if (pathString(path) == RenderModelPaths[i])
		{
			if (!st.profileBound)
			{
				// no controllers bound yet: no model, as on a Quest
				properties->modelKey = XR_NULL_RENDER_MODEL_KEY_FB;
				return XR_RENDER_MODEL_UNAVAILABLE_FB;
			}
			properties->vendorId = 0;
			strncpy(properties->modelName, "XRSIM test model", XR_MAX_RENDER_MODEL_NAME_SIZE_FB - 1);
			properties->modelKey = i + 1;
			properties->modelVersion = 1;
			properties->flags = XR_RENDER_MODEL_SUPPORTS_GLTF_2_0_SUBSET_1_BIT_FB;
			return XR_SUCCESS;
		}
	return XR_ERROR_PATH_UNSUPPORTED;
}

SIM_FN(LoadRenderModelFB)(XrSession session, const XrRenderModelLoadInfoFB *info, XrRenderModelBufferFB *buffer)
{
	if (!validSession(session))
		return XR_ERROR_HANDLE_INVALID;
	if (info == nullptr || buffer == nullptr)
		return XR_ERROR_VALIDATION_FAILURE;
	if (info->modelKey < 1 || info->modelKey > std::size(RenderModelPaths))
		return XR_ERROR_RENDER_MODEL_KEY_INVALID_FB;
	std::vector<u8> bytes;
	if (FILE *f = fopen(config::VrXrSimRenderModel.get().c_str(), "rb"))
	{
		u8 chunk[4096];
		size_t n;
		while ((n = fread(chunk, 1, sizeof(chunk), f)) > 0)
			bytes.insert(bytes.end(), chunk, chunk + n);
		fclose(f);
	}
	if (bytes.empty())
	{
		ERROR_LOG(RENDERER, "XRSIM: can't read the render model %s", config::VrXrSimRenderModel.get().c_str());
		return XR_ERROR_RUNTIME_FAILURE;
	}
	return fill(buffer->bufferCapacityInput, &buffer->bufferCountOutput, buffer->buffer, (uint32_t)bytes.size(),
			[&bytes](uint8_t& b, uint32_t i) { b = bytes[i]; });
}

#define ENTRY(name) { "xr" #name, (PFN_xrVoidFunction)sim##name }
const struct { const char *name; PFN_xrVoidFunction function; } Functions[] {
	ENTRY(GetInstanceProcAddr), ENTRY(EnumerateInstanceExtensionProperties), ENTRY(CreateInstance), ENTRY(DestroyInstance),
	ENTRY(GetInstanceProperties), ENTRY(PollEvent), ENTRY(ResultToString), ENTRY(StringToPath), ENTRY(PathToString),
	ENTRY(GetSystem), ENTRY(GetSystemProperties), ENTRY(EnumerateEnvironmentBlendModes), ENTRY(EnumerateViewConfigurations),
	ENTRY(GetViewConfigurationProperties), ENTRY(EnumerateViewConfigurationViews), ENTRY(GetOpenGLGraphicsRequirementsKHR),
	ENTRY(CreateSession), ENTRY(DestroySession), ENTRY(BeginSession), ENTRY(EndSession), ENTRY(RequestExitSession),
	ENTRY(EnumerateReferenceSpaces), ENTRY(CreateReferenceSpace), ENTRY(GetReferenceSpaceBoundsRect), ENTRY(CreateActionSpace),
	ENTRY(DestroySpace), ENTRY(LocateSpace), ENTRY(LocateViews), ENTRY(EnumerateSwapchainFormats), ENTRY(CreateSwapchain),
	ENTRY(DestroySwapchain), ENTRY(EnumerateSwapchainImages), ENTRY(AcquireSwapchainImage), ENTRY(WaitSwapchainImage),
	ENTRY(ReleaseSwapchainImage), ENTRY(WaitFrame), ENTRY(BeginFrame), ENTRY(EndFrame), ENTRY(CreateActionSet),
	ENTRY(DestroyActionSet), ENTRY(CreateAction), ENTRY(DestroyAction), ENTRY(SuggestInteractionProfileBindings),
	ENTRY(AttachSessionActionSets), ENTRY(GetCurrentInteractionProfile), ENTRY(SyncActions), ENTRY(GetActionStateBoolean),
	ENTRY(GetActionStateFloat), ENTRY(GetActionStateVector2f), ENTRY(GetActionStatePose), ENTRY(ApplyHapticFeedback),
	ENTRY(StopHapticFeedback), ENTRY(EnumerateDisplayRefreshRatesFB), ENTRY(GetDisplayRefreshRateFB),
	ENTRY(RequestDisplayRefreshRateFB),
};
// only with vr.XrSimRenderModel (a runtime has no functions for an extension it doesn't list)
// and to an instance that enabled XR_FB_render_model
const struct { const char *name; PFN_xrVoidFunction function; } RenderModelFunctions[] {
	ENTRY(EnumerateRenderModelPathsFB), ENTRY(GetRenderModelPropertiesFB), ENTRY(LoadRenderModelFB),
};
#undef ENTRY

// a number as the harness writes it (a '.' whatever the C locale says)
bool parseNumber(const std::string& s, float& value)
{
	const char *end = s.data() + s.size();
	const auto r = std::from_chars(s.data(), end, value);
	return r.ec == std::errc() && r.ptr == end;
}

}	// namespace

bool active()
{
	static const bool on = config::VrXrSim;
	return on;
}

PFN_xrVoidFunction proc(const char *name)
{
	for (const auto& f : Functions)
		if (!strcmp(f.name, name))
			return f.function;
	if (renderModels() && st.renderModelEnabled)
		for (const auto& f : RenderModelFunctions)
			if (!strcmp(f.name, name))
				return f.function;
	return nullptr;
}

void udpLine(const char *line)
{
	if (!active())
		return;
	std::vector<std::string> words;
	for (const char *p = line; *p != '\0';)
	{
		while (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n')
			p++;
		const char *start = p;
		while (*p != '\0' && *p != ' ' && *p != '\t' && *p != '\r' && *p != '\n')
			p++;
		if (p > start)
			words.emplace_back(start, p);
	}
	float v[6] {};
	if (words.size() == 3 && words[0] == "XR" && words[1].size() > 1 && words[1][0] == '/' && parseNumber(words[2], v[0]))
	{
		bool changed;
		{
			std::lock_guard<std::mutex> lock(inputMutex);
			const auto it = liveInputs.find(words[1]);
			changed = it == liveInputs.end() || it->second != v[0];
			liveInputs[words[1]] = v[0];
		}
		if (changed)
			INFO_LOG(INPUT, "XRSIM: input %s %.3f", words[1].c_str(), v[0]);
		return;
	}
	if ((words.size() == 7 || words.size() == 8) && words[0] == "XRPOSE")
	{
		int who = -1;
		for (int i = 0; i < 3; i++)
			if (words[1] == PoseNames[i])
				who = i;
		bool ok = who >= 0;
		for (size_t i = 2; ok && i < words.size(); i++)
			ok = parseNumber(words[i], v[i - 2]);
		if (ok)
		{
			const Pose pose = makePose(v[0], v[1], v[2], v[3], v[4], v[5]);
			bool changed;
			{
				std::lock_guard<std::mutex> lock(inputMutex);
				changed = livePoses[who].position != pose.position || livePoses[who].orientation != pose.orientation;
				livePoses[who] = pose;
			}
			if (changed)
			{
				std::string numbers;
				for (size_t i = 2; i < words.size(); i++)
					numbers += " " + words[i];
				INFO_LOG(INPUT, "XRSIM: pose %s%s", PoseNames[who], numbers.c_str());
			}
			return;
		}
	}
	if (words.size() == 3 && words[0] == "XRTRACK" && words[1] == "head" && (words[2] == "0" || words[2] == "1"))
	{
		const bool tracked = words[2] == "1";
		if (wantHeadTracked.exchange(tracked) != tracked)
			NOTICE_LOG(INPUT, "XRSIM: head tracking %s", tracked ? "valid" : "invalid");
		return;
	}
	if (words.size() == 2 && words[0] == "XRSTATE" && (words[1] == "focused" || words[1] == "visible"))
	{
		const bool focus = words[1] == "focused";
		if (wantFocus.exchange(focus) != focus)
			NOTICE_LOG(INPUT, "XRSIM: %s asked for", words[1].c_str());
		return;
	}
	static std::atomic<int> warnings;
	if (warnings++ < 10)
		WARN_LOG(INPUT, "XRSIM: not understood: %s", line);
}

}	// namespace vr::xr::sim
#endif
