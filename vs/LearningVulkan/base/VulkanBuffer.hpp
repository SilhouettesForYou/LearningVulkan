#pragma once
#pragma warning(disable:4244)

#include <vector>

#include "vulkan/vk_platform.h"
#include "VulkanTools.h"

namespace vks
{
    struct Buffer
    {
        VkDevice device;
        VkBuffer buffer = VK_NULL_HANDLE;
        VkDeviceMemory memory = VK_NULL_HANDLE;
        VkDescriptorBufferInfo descriptor;
        VkDeviceSize size = 0;
        VkDeviceSize aligment = 0;
        void* mapped = nullptr;

        /** @brief Usage flags to be filled by external source at buffer creation (to query at some later point) */
        VkBufferUsageFlags usageFlags;
        /** @brief Memory propertys flags to be filled by external source at buffer creation (to query at some later point) */
        VkMemoryPropertyFlags memoryPropertyFlags;

		/** 
		* Map a memory range of this buffer. If successful, mapped points to the specified buffer range.
		* 
		* @param size (Optional) Size of the memory range to map. Pass VK_WHOLE_SIZE to map the complete buffer range.
		* @param offset (Optional) Byte offset from beginning
		* 
		* @return VkResult of the buffer mapping call
		*/
        VkResult Map(VkDeviceSize size = VK_WHOLE_SIZE, VkDeviceSize offset = 0)
        {
            return vkMapMemory(device, memory, offset, size, 0, &mapped);
        }

		/**
		* Unmap a mapped memory range
		*
		* @note Does not return a result as vkUnmapMemory can't fail
		*/
        void Unmap()
        {
            if (mapped)
            {
                vkUnmapMemory(device, memory);
                mapped = nullptr;
            }
        }

		/** 
		* Attach the allocated memory block to the buffer
		* 
		* @param offset (Optional) Byte offset (from the beginning) for the memory region to bind
		* 
		* @return VkResult of the bindBufferMemory call
		*/
        VkResult Bind(VkDeviceSize offset = 0)
        {
            return vkBindBufferMemory(device, buffer, memory, offset);
        }

		/**
		* Setup the default descriptor for this buffer
		*
		* @param size (Optional) Size of the memory range of the descriptor
		* @param offset (Optional) Byte offset from beginning
		*
		*/
        void SetupDescriptor(VkDeviceSize size = VK_WHOLE_SIZE, VkDeviceSize offset = 0)
        {
            descriptor.range = size;
            descriptor.buffer = buffer;
            descriptor.offset = offset;
        }

		/**
		* Copies the specified data to the mapped buffer
		* 
		* @param data Pointer to the data to copy
		* @param size Size of the data to copy in machine units
		*
		*/
        void CopyTo(void* data, VkDeviceSize size)
        {
            assert(mapped);
            memcpy(mapped, data, size);
        }

		/** 
		* Flush a memory range of the buffer to make it visible to the device
		*
		* @note Only required for non-coherent memory
		*
		* @param size (Optional) Size of the memory range to flush. Pass VK_WHOLE_SIZE to flush the complete buffer range.
		* @param offset (Optional) Byte offset from beginning
		*
		* @return VkResult of the flush call
		*/
        VkResult Flush(VkDeviceSize size = VK_WHOLE_SIZE, VkDeviceSize offset = 0)
        {
            VkMappedMemoryRange mappedrange = {};
            mappedrange.sType = VK_STRUCTURE_TYPE_MAPPED_MEMORY_RANGE;
            mappedrange.memory = memory;
            mappedrange.offset = offset;
            mappedrange.size = size;
            return vkFlushMappedMemoryRanges(device, 1, &mappedrange);
        }

		/**
		* Invalidate a memory range of the buffer to make it visible to the host
		*
		* @note Only required for non-coherent memory
		*
		* @param size (Optional) Size of the memory range to invalidate. Pass VK_WHOLE_SIZE to invalidate the complete buffer range.
		* @param offset (Optional) Byte offset from beginning
		*
		* @return VkResult of the invalidate call
		*/
        VkResult Invalidate(VkDeviceSize size = VK_WHOLE_SIZE, VkDeviceSize offset = 0)
        {
            VkMappedMemoryRange mappedrange = {};
            mappedrange.sType = VK_STRUCTURE_TYPE_MAPPED_MEMORY_RANGE;
            mappedrange.memory = memory;
            mappedrange.offset = offset;
            mappedrange.size = size;
            return vkInvalidateMappedMemoryRanges(device, 1, &mappedrange);
        }

		/** 
		* Release all Vulkan resources held by this buffer
		*/
        void Destroy()
        {
            if (buffer)
                vkDestroyBuffer(device, buffer, nullptr);
            if (memory)
                vkFreeMemory(device, memory, nullptr);
        }
    };
}