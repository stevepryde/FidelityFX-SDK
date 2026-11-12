// Minimal Vulkan runner for repro.hlsl. SPDX-License-Identifier: MIT
#include <vulkan/vulkan.h>
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

static void check(VkResult result) {
    if (result != VK_SUCCESS) throw std::runtime_error("Vulkan error " + std::to_string(result));
}
static double half(uint32_t value) {
    uint32_t h = value & 65535, e = (h >> 10) & 31, m = h & 1023;
    double v = e ? std::ldexp(1.0 + m / 1024.0, int(e) - 15) : std::ldexp(double(m), -24);
    return h & 32768 ? -v : v;
}
struct Pixel { uint32_t x, y; };
struct Config { uint32_t width, height, scenario, excludePadding; };

int main(int argc, char** argv) try {
    if (argc != 2) throw std::runtime_error("usage: fi-edge-repro repro.spv");
    std::ifstream shader(argv[1], std::ios::binary | std::ios::ate);
    if (!shader) throw std::runtime_error("cannot open shader");
    std::vector<uint32_t> code(size_t(shader.tellg()) / 4);
    shader.seekg(0); shader.read(reinterpret_cast<char*>(code.data()), code.size() * 4);
    uint32_t count = 0;
    check(vkEnumerateInstanceExtensionProperties(nullptr, &count, nullptr));
    std::vector<VkExtensionProperties> extensions(count);
    check(vkEnumerateInstanceExtensionProperties(nullptr, &count, extensions.data()));
    std::vector<const char*> names;
    for (auto e : extensions) if (!std::strcmp(e.extensionName, "VK_KHR_portability_enumeration")) names.push_back("VK_KHR_portability_enumeration");
    VkApplicationInfo app{VK_STRUCTURE_TYPE_APPLICATION_INFO}; app.apiVersion = VK_API_VERSION_1_1;
    VkInstanceCreateInfo ici{VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO}; ici.pApplicationInfo = &app;
    ici.enabledExtensionCount = uint32_t(names.size()); ici.ppEnabledExtensionNames = names.data();
    if (!names.empty()) ici.flags = VK_INSTANCE_CREATE_ENUMERATE_PORTABILITY_BIT_KHR;
    VkInstance instance; check(vkCreateInstance(&ici, nullptr, &instance));
    check(vkEnumeratePhysicalDevices(instance, &count, nullptr));
    if (!count) throw std::runtime_error("no Vulkan device");
    std::vector<VkPhysicalDevice> physicals(count); check(vkEnumeratePhysicalDevices(instance, &count, physicals.data()));
    VkPhysicalDevice physical = physicals[0];
    VkPhysicalDeviceProperties properties; vkGetPhysicalDeviceProperties(physical, &properties);
    std::cout << "Device: " << properties.deviceName << '\n';
    vkGetPhysicalDeviceQueueFamilyProperties(physical, &count, nullptr);
    std::vector<VkQueueFamilyProperties> families(count); vkGetPhysicalDeviceQueueFamilyProperties(physical, &count, families.data());
    uint32_t family = 0; while (family < count && !(families[family].queueFlags & VK_QUEUE_COMPUTE_BIT)) ++family;
    if (family == count) throw std::runtime_error("no compute queue");
    float priority = 1;
    VkDeviceQueueCreateInfo qci{VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO}; qci.queueFamilyIndex = family; qci.queueCount = 1; qci.pQueuePriorities = &priority;
    check(vkEnumerateDeviceExtensionProperties(physical, nullptr, &count, nullptr));
    extensions.resize(count); check(vkEnumerateDeviceExtensionProperties(physical, nullptr, &count, extensions.data()));
    names.clear(); for (auto e : extensions) if (!std::strcmp(e.extensionName, "VK_KHR_portability_subset")) names.push_back("VK_KHR_portability_subset");
    VkDeviceCreateInfo dci{VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO}; dci.queueCreateInfoCount = 1; dci.pQueueCreateInfos = &qci;
    dci.enabledExtensionCount = uint32_t(names.size()); dci.ppEnabledExtensionNames = names.data();
    VkDevice device; check(vkCreateDevice(physical, &dci, nullptr, &device));
    VkQueue queue; vkGetDeviceQueue(device, family, 0, &queue);
    const VkDeviceSize bytes = 1200 * 750 * sizeof(Pixel);
    VkBufferCreateInfo bci{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO}; bci.size = bytes; bci.usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;
    VkBuffer buffer; check(vkCreateBuffer(device, &bci, nullptr, &buffer));
    VkMemoryRequirements requirements; vkGetBufferMemoryRequirements(device, buffer, &requirements);
    VkPhysicalDeviceMemoryProperties memoryProperties; vkGetPhysicalDeviceMemoryProperties(physical, &memoryProperties);
    uint32_t type = 0; const auto flags = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
    while (type < memoryProperties.memoryTypeCount && (!(requirements.memoryTypeBits & (1u << type)) || (memoryProperties.memoryTypes[type].propertyFlags & flags) != flags)) ++type;
    if (type == memoryProperties.memoryTypeCount) throw std::runtime_error("no coherent host-visible storage memory");
    VkMemoryAllocateInfo mai{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO}; mai.allocationSize = requirements.size; mai.memoryTypeIndex = type;
    VkDeviceMemory memory; check(vkAllocateMemory(device, &mai, nullptr, &memory)); check(vkBindBufferMemory(device, buffer, memory, 0));
    void* mapped; check(vkMapMemory(device, memory, 0, bytes, 0, &mapped));
    VkDescriptorSetLayoutBinding binding{0, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr};
    VkDescriptorSetLayoutCreateInfo slci{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO}; slci.bindingCount = 1; slci.pBindings = &binding;
    VkDescriptorSetLayout setLayout; check(vkCreateDescriptorSetLayout(device, &slci, nullptr, &setLayout));
    VkPushConstantRange push{VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(Config)};
    VkPipelineLayoutCreateInfo plci{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO}; plci.setLayoutCount = 1; plci.pSetLayouts = &setLayout; plci.pushConstantRangeCount = 1; plci.pPushConstantRanges = &push;
    VkPipelineLayout layout; check(vkCreatePipelineLayout(device, &plci, nullptr, &layout));
    VkShaderModuleCreateInfo smci{VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO}; smci.codeSize = code.size() * 4; smci.pCode = code.data();
    VkShaderModule module; check(vkCreateShaderModule(device, &smci, nullptr, &module));
    VkComputePipelineCreateInfo pci{VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO}; pci.layout = layout;
    pci.stage = {VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, nullptr, 0, VK_SHADER_STAGE_COMPUTE_BIT, module, "main", nullptr};
    VkPipeline pipeline; check(vkCreateComputePipelines(device, VK_NULL_HANDLE, 1, &pci, nullptr, &pipeline));
    VkDescriptorPoolSize poolSize{VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1};
    VkDescriptorPoolCreateInfo dpci{VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO}; dpci.maxSets = 1; dpci.poolSizeCount = 1; dpci.pPoolSizes = &poolSize;
    VkDescriptorPool descriptorPool; check(vkCreateDescriptorPool(device, &dpci, nullptr, &descriptorPool));
    VkDescriptorSetAllocateInfo dsai{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO}; dsai.descriptorPool = descriptorPool; dsai.descriptorSetCount = 1; dsai.pSetLayouts = &setLayout;
    VkDescriptorSet descriptor; check(vkAllocateDescriptorSets(device, &dsai, &descriptor));
    VkDescriptorBufferInfo bufferInfo{buffer, 0, bytes};
    VkWriteDescriptorSet write{VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET}; write.dstSet = descriptor; write.descriptorCount = 1; write.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER; write.pBufferInfo = &bufferInfo;
    vkUpdateDescriptorSets(device, 1, &write, 0, nullptr);
    VkCommandPoolCreateInfo cpci{VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO}; cpci.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT; cpci.queueFamilyIndex = family;
    VkCommandPool pool; check(vkCreateCommandPool(device, &cpci, nullptr, &pool));
    VkCommandBufferAllocateInfo cbai{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO}; cbai.commandPool = pool; cbai.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY; cbai.commandBufferCount = 1;
    VkCommandBuffer command; check(vkAllocateCommandBuffers(device, &cbai, &command));
    for (Config config : {Config{32, 750, 0, 0}, Config{32, 750, 0, 1}, Config{32, 750, 2, 0}, Config{32, 750, 2, 1}, Config{1200, 750, 1, 1}}) {
        std::memset(mapped, 0, size_t(bytes));
        check(vkResetCommandBuffer(command, 0));
        VkCommandBufferBeginInfo begin{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO}; check(vkBeginCommandBuffer(command, &begin));
        vkCmdBindPipeline(command, VK_PIPELINE_BIND_POINT_COMPUTE, pipeline);
        vkCmdBindDescriptorSets(command, VK_PIPELINE_BIND_POINT_COMPUTE, layout, 0, 1, &descriptor, 0, nullptr);
        vkCmdPushConstants(command, layout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(config), &config);
        vkCmdDispatch(command, (config.width + 7) / 8, (config.height + 7) / 8, 1);
        VkMemoryBarrier barrier{VK_STRUCTURE_TYPE_MEMORY_BARRIER}; barrier.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT; barrier.dstAccessMask = VK_ACCESS_HOST_READ_BIT;
        vkCmdPipelineBarrier(command, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_HOST_BIT, 0, 1, &barrier, 0, nullptr, 0, nullptr);
        check(vkEndCommandBuffer(command));
        VkSubmitInfo submit{VK_STRUCTURE_TYPE_SUBMIT_INFO}; submit.commandBufferCount = 1; submit.pCommandBuffers = &command;
        check(vkQueueSubmit(queue, 1, &submit, VK_NULL_HANDLE)); check(vkQueueWaitIdle(queue));
        auto pixels = static_cast<Pixel*>(mapped);
        if (config.scenario != 1) {
            // A valid constant translation must still reach this interior
            // target with half the input motion. A no-op shader is not a fix.
            const Pixel interior = pixels[200 * config.width + 16];
            if (!(interior.x >> 31) || !(interior.y >> 31) || half(interior.x) != 0 || half(interior.y) != -.125)
                throw std::runtime_error("valid interior translation was not reconstructed");
            uint32_t primary = 0;
            for (uint32_t y = 700; y < config.height; ++y)
                for (uint32_t x = 0; x < config.width; ++x) primary += pixels[y * config.width + x].y >> 31;
            std::cout << (config.scenario == 0 ? "Zero-filled padding" : "Allocated nonzero padding")
                      << ", excluded=" << config.excludePadding << ": " << primary
                      << " primary vectors in outgoing rows700..749; expected 0; valid interior preserved\n";
            if (config.excludePadding && primary) throw std::runtime_error("bounded negative control failed");
        } else {
            // Independent visible-plane control: the primary at row400 must
            // remain close to its analytic correspondence. One pixel allows
            // the SDK's nearest-depth dilation and bilinear source footprint.
            const Pixel interior = pixels[400 * config.width + 600];
            const double controlA = ((400.5 / config.height) - .5) * std::tan(40.0 * std::acos(-1.0) / 180.0);
            const double controlZ = (1 - 4*controlA + std::sqrt(1 + 16*controlA*controlA)) / (2*controlA);
            const double controlHalf = -((400.5 / config.height) - .5) * 4 / (2*controlZ + 4) * config.height;
            if (!(interior.y >> 31) || std::abs(half(interior.y) * config.height - controlHalf) > 1)
                throw std::runtime_error("visible plane does not match its analytic correspondence");
            double maxHalf = 0;
            for (uint32_t i = 0; i < config.width * config.height; ++i) maxHalf = std::max(maxHalf, std::abs(half(pixels[i].y)) * config.height);
            double v = 700.5 / config.height, a = (v - .5) * std::tan(40.0 * std::acos(-1.0) / 180.0);
            double z = (1 - 4*a + std::sqrt(1 + 16*a*a)) / (2*a);
            double required = (v - .5) * 4 / (2*z + 4) * config.height;
            auto center = pixels[700 * config.width + 600];
            std::cout << "Ground, padding excluded: maximum generated vertical half-motion " << maxHalf
                      << " px; required at (600,700) " << required << " px; actual " << -half(center.y) * config.height
                      << " px; packed entry 0x" << std::hex << center.y << std::dec << '\n';
            std::ofstream dump("ground-field.bin", std::ios::binary); dump.write(static_cast<char*>(mapped), config.width * config.height * sizeof(Pixel));
            std::cout << (maxHalf + 1 < required ? "REPRODUCED: no available vector can represent the required outgoing motion\n" : "Source range now covers the diagnostic motion; inspect the field against the analytic projection\n");
        }
    }
    vkUnmapMemory(device, memory); vkDestroyCommandPool(device, pool, nullptr);
    vkDestroyDescriptorPool(device, descriptorPool, nullptr); vkDestroyPipeline(device, pipeline, nullptr);
    vkDestroyShaderModule(device, module, nullptr); vkDestroyPipelineLayout(device, layout, nullptr);
    vkDestroyDescriptorSetLayout(device, setLayout, nullptr); vkDestroyBuffer(device, buffer, nullptr);
    vkFreeMemory(device, memory, nullptr); vkDestroyDevice(device, nullptr); vkDestroyInstance(instance, nullptr);
    return 0;
} catch (const std::exception& e) { std::cerr << e.what() << '\n'; return 1; }
