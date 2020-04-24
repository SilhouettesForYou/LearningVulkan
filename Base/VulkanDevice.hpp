#pragma once

#include <exception>
#include <assert.h>
#include <algorithm>
#include <vector>
#include "vulkan/vulkan.h"
#include "VulkanTools.h"
#include "VulkanBuffer.hpp"


namespace vks
{
    struct VulkanDevice
    {
        /** @brief Physical device representation */
		VkPhysicalDevice physicalDevice;
		/** @brief Logical device representation (application's view of the device) */
		VkDevice logicalDevice;
		/** @brief Properties of the physical device including limits that the application can check against */
		VkPhysicalDeviceProperties properties;
		/** @brief Features of the physical device that an application can use to check if a feature is supported */
		VkPhysicalDeviceFeatures features;
		/** @brief Features that have been enabled for use on the physical device */
		VkPhysicalDeviceFeatures enabledFeatures;
		/** @brief Memory types and heaps of the physical device */
		VkPhysicalDeviceMemoryProperties memoryProperties;
		/** @brief Queue family properties of the physical device */
		std::vector<VkQueueFamilyProperties> queueFamilyProperties;
		/** @brief List of extensions supported by the device */
		std::vector<std::string> supportedExtensions;

        /** @brief Default command pool for the graphics queue family index */
        VkCommandPool commandPool = VK_NULL_HANDLE;

        /** @brief Set to true when the debug marker extension is detected */
		bool enableDebugMarkers = false;

        /** @brief Contains queue family indices */
        struct
        {
            uint32_t graphics;
            uint32_t compute;
            uint32_t transfer;
        } queueFamilyIndices;

        /**  @brief Typecast to VkDevice */
        operator VkDevice() 
        {
            return logicalDevice;
        }

        VulkanDevice(VkPhysicalDevice physicalDevice)
        {
            assert(physicalDevice);
            this->physicalDevice = physicalDevice;

			// Store Properties features, limits and properties of the physical device for later use
			// Device properties also contain limits and sparse properties
            vkGetPhysicalDeviceProperties(physicalDevice, &properties);
            // Features should be checked by the examples before using them
            vkGetPhysicalDeviceFeatures(physicalDevice, features);
            // Memory properties are used regulary for creating all kinds of buffers
            vkGetPhysicalDeviceMemoryProperties(physicalDevice, &memoryProperties);
            // Queue family properties, used for setting up requested queues upon device creation
            uint32_t queueFamilyCount;
            vkGetPhysicalDeviceQueueFamilyProperties(physicalDevice, &queueFamilyCount, nullptr);
            if (queueFamilyCount > 0)
            {
                queueFamilyProperties.resize(queueFamilyCount);
                vkGetPhysicalDeviceQueueFamilyProperties(physicalDevice, &queueFamilyCount, queueFamilyProperties.data());
            }

            // Get list of supported extensions
			uint32_t extCount = 0;
			vkEnumerateDeviceExtensionProperties(physicalDevice, nullptr, &extCount, nullptr);
			if (extCount > 0)
			{
				std::vector<VkExtensionProperties> extensions(extCount);
				if (vkEnumerateDeviceExtensionProperties(physicalDevice, nullptr, &extCount, &extensions.front()) == VK_SUCCESS)
				{
					for (auto ext : extensions)
					{
						supportedExtensions.push_back(ext.extensionName);
					}
				}
			}
        }

        ~VulkanDevice()
        {
            if (commandPool)
                vkDestroyCommandPool(logicalDevice, commandPool, nullptr);
            if (logicalDevice)
                vkDestroyDevice(logicalDevice, nullptr);
        }

        uint32_t GetQueueFamilyIndex(VkQueueFlagBits queueFlags)
        {
            
        }

        uint32_t GetMemoryType(uint32_t typeBits, VkMemoryPropertyFlags properties, VkBool32* memTypeFound = nullptr)
        {
            for (uint32_t i = 0; i < memoryProperties.memoryTypeCount; i++)
            {
                if ((typeBits & 1) == 1)
                {
                    if ((memoryProperties.memoryTypes[i].propertyFlags & properties) == properties)
                    {
                        if (memTypeFound)
                            *memTypeFound = true;
                        return i;
                    }
                }
                typeBits >>= 1;
            }

            if (memTypeFound)
            {
                *memTypeFound = false;
                return 0;
            }
            else
            {
                throw std::runtime_error("Could not find a matching memory type");
            }
        }

        VkResult CreateLogicalDevice(
            VkPhysicalDeviceFeatures enabledFeatures, 
            std::vector<const char*> enabledExtension, 
            void* pNextChain,
            bool useSwapChain = true,
            VkQueueFlags requestQueueTypes = VK_QUEUE_GRAPHICS_BIT | VK_QUEUE_COMPUTE_BIT)
            {
                // Desired queues need to be requested upon logical device creation
                // Due to differing queue family configurations of Vulkan implementations this can be a bit tricky, especially if the application
                // requests different queue types

                std::vector<VkDeviceQueueCreateInfo> queueCreateInfos{};

                // Get queue family indices for the requested queue family types
			    // Note that the indices may overlap depending on the implementation
                
                const float defaultQueuePriority(0.0f);

                // Graphics queue
                if (requestedQueueTypes & VK_QUEUE_GRAPHICS_BIT)
                {
                    queueFamilyIndices.graphics = GetQueueFamilyIndex()
                }
            }

    }
}