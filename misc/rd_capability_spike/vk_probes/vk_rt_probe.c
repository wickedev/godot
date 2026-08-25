// Minimal Vulkan probe: does this device expose the ray tracing extensions Godot needs?
// Self-contained -- links only against the system Vulkan loader.
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <vulkan/vulkan.h>

static const char *WANTED[] = {
    VK_KHR_ACCELERATION_STRUCTURE_EXTENSION_NAME,
    VK_KHR_RAY_TRACING_PIPELINE_EXTENSION_NAME,
    VK_KHR_RAY_QUERY_EXTENSION_NAME,
    VK_KHR_DEFERRED_HOST_OPERATIONS_EXTENSION_NAME,
    VK_KHR_BUFFER_DEVICE_ADDRESS_EXTENSION_NAME,
    VK_EXT_DESCRIPTOR_INDEXING_EXTENSION_NAME,
    VK_EXT_CALIBRATED_TIMESTAMPS_EXTENSION_NAME,
};
#define NWANTED (sizeof(WANTED)/sizeof(WANTED[0]))

int main(void) {
    VkApplicationInfo app = {VK_STRUCTURE_TYPE_APPLICATION_INFO};
    app.pApplicationName = "rt_probe";
    app.apiVersion = VK_API_VERSION_1_3;
    VkInstanceCreateInfo ici = {VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO};
    ici.pApplicationInfo = &app;

    VkInstance inst;
    if (vkCreateInstance(&ici, NULL, &inst) != VK_SUCCESS) {
        printf("RESULT instance_create=FAILED\n");
        return 1;
    }
    uint32_t n = 0;
    vkEnumeratePhysicalDevices(inst, &n, NULL);
    printf("RESULT physical_device_count=%u\n", n);
    if (!n) return 1;
    VkPhysicalDevice *devs = calloc(n, sizeof(*devs));
    vkEnumeratePhysicalDevices(inst, &n, devs);

    for (uint32_t d = 0; d < n; d++) {
        VkPhysicalDeviceProperties props;
        vkGetPhysicalDeviceProperties(devs[d], &props);
        printf("RESULT device[%u]=%s api=%u.%u.%u\n", d, props.deviceName,
               VK_VERSION_MAJOR(props.apiVersion), VK_VERSION_MINOR(props.apiVersion),
               VK_VERSION_PATCH(props.apiVersion));
        printf("RESULT device[%u]_timestamp_period=%f limits.timestampComputeAndGraphics=%u\n",
               d, props.limits.timestampPeriod, props.limits.timestampComputeAndGraphics);

        uint32_t ec = 0;
        vkEnumerateDeviceExtensionProperties(devs[d], NULL, &ec, NULL);
        VkExtensionProperties *ex = calloc(ec, sizeof(*ex));
        vkEnumerateDeviceExtensionProperties(devs[d], NULL, &ec, ex);
        for (size_t w = 0; w < NWANTED; w++) {
            int found = 0;
            for (uint32_t i = 0; i < ec; i++)
                if (!strcmp(ex[i].extensionName, WANTED[w])) { found = 1; break; }
            printf("RESULT device[%u]_ext %-45s %s\n", d, WANTED[w], found ? "YES" : "no");
        }

        // Descriptor indexing feature bits -- the (c) verdict from the RD spike.
        VkPhysicalDeviceVulkan12Features f12 = {VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES};
        VkPhysicalDeviceFeatures2 f2 = {VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2};
        f2.pNext = &f12;
        vkGetPhysicalDeviceFeatures2(devs[d], &f2);
        printf("RESULT device[%u]_shaderSampledImageArrayNonUniformIndexing=%u\n", d, f12.shaderSampledImageArrayNonUniformIndexing);
        printf("RESULT device[%u]_runtimeDescriptorArray=%u\n", d, f12.runtimeDescriptorArray);
        printf("RESULT device[%u]_descriptorBindingPartiallyBound=%u\n", d, f12.descriptorBindingPartiallyBound);
        printf("RESULT device[%u]_descriptorBindingVariableDescriptorCount=%u\n", d, f12.descriptorBindingVariableDescriptorCount);
        printf("RESULT device[%u]_bufferDeviceAddress=%u\n", d, f12.bufferDeviceAddress);
        free(ex);
    }
    return 0;
}
