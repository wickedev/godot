// Which vertex formats can actually feed an acceleration-structure build on this device?
// Queries VK_FORMAT_FEATURE_2_ACCELERATION_STRUCTURE_VERTEX_BUFFER_BIT_KHR directly --
// this is the authoritative answer to the G2 quantized-position question.
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <vulkan/vulkan.h>

typedef struct { VkFormat f; const char *name; const char *note; } Cand;

static Cand CANDS[] = {
    {VK_FORMAT_R32G32B32_SFLOAT,     "R32G32B32_SFLOAT",     "spec-mandatory, 12B, uncompressed baseline"},
    {VK_FORMAT_R32G32_SFLOAT,        "R32G32_SFLOAT",        "spec-mandatory"},
    {VK_FORMAT_R16G16B16A16_SFLOAT,  "R16G16B16A16_SFLOAT",  "spec-mandatory, 8B, half"},
    {VK_FORMAT_R16G16B16A16_SNORM,   "R16G16B16A16_SNORM",   "spec-mandatory, 8B <-- G2 proposal"},
    {VK_FORMAT_R16G16_SFLOAT,        "R16G16_SFLOAT",        "spec-mandatory"},
    {VK_FORMAT_R16G16_SNORM,         "R16G16_SNORM",         "spec-mandatory"},
    {VK_FORMAT_R16G16B16_SNORM,      "R16G16B16_SNORM",      "NOT mandatory, 6B, would save 2B/vtx"},
    {VK_FORMAT_R16G16B16_SFLOAT,     "R16G16B16_SFLOAT",     "NOT mandatory, 6B"},
    {VK_FORMAT_R16G16B16A16_UNORM,   "R16G16B16A16_UNORM",   "optional"},
    {VK_FORMAT_A2B10G10R10_UNORM_PACK32, "A2B10G10R10_UNORM_PACK32", "optional, 4B <-- tightest packing"},
    {VK_FORMAT_R8G8B8A8_SNORM,       "R8G8B8A8_SNORM",       "optional, 4B"},
    {VK_FORMAT_R8G8B8A8_UNORM,       "R8G8B8A8_UNORM",       "optional, 4B"},
};
#define NCAND (sizeof(CANDS)/sizeof(CANDS[0]))

int main(void) {
    VkApplicationInfo app = {VK_STRUCTURE_TYPE_APPLICATION_INFO};
    app.apiVersion = VK_API_VERSION_1_3;
    VkInstanceCreateInfo ici = {VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO};
    ici.pApplicationInfo = &app;
    VkInstance inst;
    if (vkCreateInstance(&ici, NULL, &inst) != VK_SUCCESS) { printf("instance FAILED\n"); return 1; }

    uint32_t n = 0; vkEnumeratePhysicalDevices(inst, &n, NULL);
    VkPhysicalDevice *devs = calloc(n, sizeof(*devs));
    vkEnumeratePhysicalDevices(inst, &n, devs);

    for (uint32_t d = 0; d < n; d++) {
        VkPhysicalDeviceProperties props;
        vkGetPhysicalDeviceProperties(devs[d], &props);
        if (!strstr(props.deviceName, "GB10")) continue;   // real hardware only
        printf("=== %s ===\n", props.deviceName);
        for (size_t i = 0; i < NCAND; i++) {
            VkFormatProperties3 p3 = {VK_STRUCTURE_TYPE_FORMAT_PROPERTIES_3};
            VkFormatProperties2 p2 = {VK_STRUCTURE_TYPE_FORMAT_PROPERTIES_2};
            p2.pNext = &p3;
            vkGetPhysicalDeviceFormatProperties2(devs[d], CANDS[i].f, &p2);
            int ok = (p3.bufferFeatures & VK_FORMAT_FEATURE_2_ACCELERATION_STRUCTURE_VERTEX_BUFFER_BIT_KHR) != 0;
            printf("RESULT as_vertex %-28s %-3s   (%s)\n", CANDS[i].name, ok ? "YES" : "no", CANDS[i].note);
        }
    }
    return 0;
}
