#define _CRT_SECURE_NO_WARNINGS
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

#include <vulkan/vulkan.h>
#include <vk_video/vulkan_video_codec_h264std.h>
#include <vk_video/vulkan_video_codec_h264std_decode.h>

#define VK_CHECK(x)                                                                     \
	do                                                                                  \
	{                                                                                   \
		VkResult _err = (x);                                                            \
		if (_err != VK_SUCCESS)                                                         \
		{                                                                               \
			fprintf(stderr, "Fatal: VkResult %d at %s:%d\n", _err, __FILE__, __LINE__); \
			exit(EXIT_FAILURE);                                                         \
		}                                                                               \
	} while (0)

// Forward declarations
static VkInstance create_vulkan_instance(void);
static VkPhysicalDevice select_video_capable_device(VkInstance instance, uint32_t *videoQueueFamily);
static VkDevice create_logical_device(VkPhysicalDevice physicalDevice, uint32_t videoQueueFamily);
static void query_video_capabilities(VkInstance instance, VkPhysicalDevice physicalDevice);
static void cleanup_vulkan(VkInstance instance, VkDevice device);

// Helper functions
static int has_extension(uint32_t count, const VkExtensionProperties *exts, const char *name)
{
	for (uint32_t i = 0; i < count; ++i)
	{
		if (strcmp(exts[i].extensionName, name) == 0)
			return 1;
	}
	return 0;
}

static int has_layer(uint32_t count, const VkLayerProperties *layers, const char *name)
{
	for (uint32_t i = 0; i < count; ++i)
	{
		if (strcmp(layers[i].layerName, name) == 0)
			return 1;
	}
	return 0;
}

int main(void)
{
	// Create Vulkan instance
	VkInstance instance = create_vulkan_instance();

	// Select a GPU with video decode capabilities
	uint32_t videoQueueFamily;
	VkPhysicalDevice physicalDevice = select_video_capable_device(instance, &videoQueueFamily);

	// Create logical device
	VkDevice device = create_logical_device(physicalDevice, videoQueueFamily);

	// Query and display video capabilities
	query_video_capabilities(instance, physicalDevice);

	// Clean up
	cleanup_vulkan(instance, device);

	fprintf(stdout, "Success: basic Vulkan Video H.264 decode path is available.\n");
	return EXIT_SUCCESS;
}

static void add_validation_layer_if_available(VkInstanceCreateInfo *ici)
{
	uint32_t layerCount = 0;
	vkEnumerateInstanceLayerProperties(&layerCount, NULL);
	VkLayerProperties *layers = layerCount ? (VkLayerProperties *)malloc(sizeof(VkLayerProperties) * layerCount) : NULL;
	if (layers)
		vkEnumerateInstanceLayerProperties(&layerCount, layers);

	static const char *enabledLayers[1] = {"VK_LAYER_KHRONOS_validation"};
	if (layers && has_layer(layerCount, layers, "VK_LAYER_KHRONOS_validation"))
	{
		ici->enabledLayerCount = 1;
		ici->ppEnabledLayerNames = enabledLayers;
		fprintf(stdout, "Info: enabling validation layer\n");
	}
	else
	{
		ici->enabledLayerCount = 0;
		ici->ppEnabledLayerNames = NULL;
	}
	if (layers)
		free(layers);
}

static VkInstance create_vulkan_instance(void)
{
	VkApplicationInfo app = {
		.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO,
		.pApplicationName = "vkvideo_min_c",
		.applicationVersion = VK_MAKE_API_VERSION(0, 1, 0, 0),
		.pEngineName = "none",
		.engineVersion = VK_MAKE_API_VERSION(0, 1, 0, 0),
		.apiVersion = VK_API_VERSION_1_3};

	VkInstanceCreateInfo ici = {
		.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO,
		.pApplicationInfo = &app,
		.enabledExtensionCount = 0,
		.ppEnabledExtensionNames = NULL};

	// Add validation layer if available
	add_validation_layer_if_available(&ici);

	VkInstance instance = VK_NULL_HANDLE;
	VK_CHECK(vkCreateInstance(&ici, NULL, &instance));

	return instance;
}

static VkPhysicalDevice select_video_capable_device(VkInstance instance, uint32_t *videoQueueFamily)
{
	// Enumerate physical devices
	uint32_t gpuCount = 0;
	VK_CHECK(vkEnumeratePhysicalDevices(instance, &gpuCount, NULL));
	if (gpuCount == 0)
	{
		fprintf(stderr, "No Vulkan devices found.\n");
		exit(EXIT_FAILURE);
	}
	VkPhysicalDevice *gpus = (VkPhysicalDevice *)malloc(sizeof(VkPhysicalDevice) * gpuCount);
	VK_CHECK(vkEnumeratePhysicalDevices(instance, &gpuCount, gpus));

	const char *REQ_DEV_EXTS[] = {
		VK_KHR_VIDEO_QUEUE_EXTENSION_NAME,		  // "VK_KHR_video_queue"
		VK_KHR_VIDEO_DECODE_QUEUE_EXTENSION_NAME, // "VK_KHR_video_decode_queue"
		VK_KHR_VIDEO_DECODE_H264_EXTENSION_NAME	  // "VK_KHR_video_decode_h264"
	};

	VkPhysicalDevice chosen = VK_NULL_HANDLE;
	uint32_t chosenVideoQF = UINT32_MAX;

	for (uint32_t gi = 0; gi < gpuCount; ++gi)
	{
		VkPhysicalDevice dev = gpus[gi];

		// Check device extensions
		uint32_t extCount = 0;
		vkEnumerateDeviceExtensionProperties(dev, NULL, &extCount, NULL);
		VkExtensionProperties *exts = extCount ? (VkExtensionProperties *)malloc(sizeof(VkExtensionProperties) * extCount) : NULL;
		if (exts)
			vkEnumerateDeviceExtensionProperties(dev, NULL, &extCount, exts);

		int ok = 1;
		for (uint32_t k = 0; k < (uint32_t)(sizeof(REQ_DEV_EXTS) / sizeof(REQ_DEV_EXTS[0])); ++k)
		{
			if (!has_extension(extCount, exts, REQ_DEV_EXTS[k]))
			{
				ok = 0;
				break;
			}
		}
		if (!ok)
		{
			if (exts)
				free(exts);
			continue;
		}

		// Find a queue family with video decode capability
		uint32_t qCount = 0;
		vkGetPhysicalDeviceQueueFamilyProperties(dev, &qCount, NULL);

		// We need the KHR video properties via the *2 call
		uint32_t q2Count = qCount;
		VkQueueFamilyProperties2 *qprops2 = (VkQueueFamilyProperties2 *)calloc(q2Count, sizeof(VkQueueFamilyProperties2));
		VkQueueFamilyVideoPropertiesKHR *vqprops = (VkQueueFamilyVideoPropertiesKHR *)calloc(q2Count, sizeof(VkQueueFamilyVideoPropertiesKHR));

		for (uint32_t i = 0; i < q2Count; ++i)
		{
			qprops2[i].sType = VK_STRUCTURE_TYPE_QUEUE_FAMILY_PROPERTIES_2;
			vqprops[i].sType = VK_STRUCTURE_TYPE_QUEUE_FAMILY_VIDEO_PROPERTIES_KHR;
			qprops2[i].pNext = &vqprops[i];
		}
		vkGetPhysicalDeviceQueueFamilyProperties2(dev, &q2Count, qprops2);

		uint32_t foundFamily = UINT32_MAX;
		for (uint32_t i = 0; i < q2Count; ++i)
		{
			// Look for decode capability
			if (vqprops[i].videoCodecOperations & VK_VIDEO_CODEC_OPERATION_DECODE_H264_BIT_KHR)
			{
				foundFamily = i;
				break;
			}
		}

		free(qprops2);
		free(vqprops);

		if (exts)
			free(exts);

		if (foundFamily != UINT32_MAX)
		{
			chosen = dev;
			chosenVideoQF = foundFamily;
			break;
		}
	}

	free(gpus);

	if (chosen == VK_NULL_HANDLE)
	{
		fprintf(stderr, "No device found with H.264 decode support via Vulkan Video.\n");
		exit(EXIT_FAILURE);
	}

	VkPhysicalDeviceProperties props;
	vkGetPhysicalDeviceProperties(chosen, &props);
	fprintf(stdout, "Chosen GPU: %s\n", props.deviceName);
	fprintf(stdout, "Video decode queue family index: %u\n", chosenVideoQF);

	*videoQueueFamily = chosenVideoQF;
	return chosen;
}

static VkDevice create_logical_device(VkPhysicalDevice physicalDevice, uint32_t videoQueueFamily)
{
	const char *REQ_DEV_EXTS[] = {
		VK_KHR_VIDEO_QUEUE_EXTENSION_NAME,		  // "VK_KHR_video_queue"
		VK_KHR_VIDEO_DECODE_QUEUE_EXTENSION_NAME, // "VK_KHR_video_decode_queue"
		VK_KHR_VIDEO_DECODE_H264_EXTENSION_NAME	  // "VK_KHR_video_decode_h264"
	};

	float prio = 1.0f;
	VkDeviceQueueCreateInfo qci = {.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO};
	qci.queueFamilyIndex = videoQueueFamily;
	qci.queueCount = 1;
	qci.pQueuePriorities = &prio;

	VkDeviceCreateInfo dci = {.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO};
	dci.queueCreateInfoCount = 1;
	dci.pQueueCreateInfos = &qci;
	dci.enabledExtensionCount = (uint32_t)(sizeof(REQ_DEV_EXTS) / sizeof(REQ_DEV_EXTS[0]));
	dci.ppEnabledExtensionNames = REQ_DEV_EXTS;

	VkDevice device = VK_NULL_HANDLE;
	VK_CHECK(vkCreateDevice(physicalDevice, &dci, NULL, &device));

	return device;
}

static void query_video_capabilities(VkInstance instance, VkPhysicalDevice physicalDevice)
{
	PFN_vkGetPhysicalDeviceVideoCapabilitiesKHR pfnGetVideoCaps =
		(PFN_vkGetPhysicalDeviceVideoCapabilitiesKHR)
			vkGetInstanceProcAddr(instance, "vkGetPhysicalDeviceVideoCapabilitiesKHR");

	if (!pfnGetVideoCaps)
	{
		fprintf(stderr, "Warning: vkGetPhysicalDeviceVideoCapabilitiesKHR not found (loader). Skipping caps query.\n");
		return;
	}

	VkVideoDecodeH264ProfileInfoKHR h264Profile = {
		.sType = VK_STRUCTURE_TYPE_VIDEO_DECODE_H264_PROFILE_INFO_KHR,
		.pNext = NULL,
		.stdProfileIdc = STD_VIDEO_H264_PROFILE_IDC_MAIN, // try Main as a common baseline
		.pictureLayout = VK_VIDEO_DECODE_H264_PICTURE_LAYOUT_PROGRESSIVE_KHR};

	VkVideoProfileInfoKHR videoProfile = {
		.sType = VK_STRUCTURE_TYPE_VIDEO_PROFILE_INFO_KHR,
		.pNext = &h264Profile,
		.videoCodecOperation = VK_VIDEO_CODEC_OPERATION_DECODE_H264_BIT_KHR,
		.chromaSubsampling = VK_VIDEO_CHROMA_SUBSAMPLING_420_BIT_KHR,
		.lumaBitDepth = VK_VIDEO_COMPONENT_BIT_DEPTH_8_BIT_KHR,
		.chromaBitDepth = VK_VIDEO_COMPONENT_BIT_DEPTH_8_BIT_KHR};

	VkVideoDecodeH264CapabilitiesKHR h264Caps = {
		.sType = VK_STRUCTURE_TYPE_VIDEO_DECODE_H264_CAPABILITIES_KHR,
		.pNext = NULL};

	VkVideoDecodeCapabilitiesKHR decodeCaps = {
		.sType = VK_STRUCTURE_TYPE_VIDEO_DECODE_CAPABILITIES_KHR,
		.pNext = &h264Caps};

	VkVideoCapabilitiesKHR caps = {
		.sType = VK_STRUCTURE_TYPE_VIDEO_CAPABILITIES_KHR,
		.pNext = &decodeCaps};

	VkResult rc = pfnGetVideoCaps(physicalDevice, &videoProfile, &caps);
	if (rc == VK_SUCCESS)
	{
		fprintf(stdout, "Video caps OK. Max coded extent: %ux%u, Max DPB slots: %u\n",
				caps.maxCodedExtent.width, caps.maxCodedExtent.height, caps.maxDpbSlots);
	}
	else
	{
		fprintf(stderr, "vkGetPhysicalDeviceVideoCapabilitiesKHR failed: %d\n", rc);
	}
}

static void cleanup_vulkan(VkInstance instance, VkDevice device)
{
	vkDeviceWaitIdle(device);
	vkDestroyDevice(device, NULL);
	vkDestroyInstance(instance, NULL);
}
