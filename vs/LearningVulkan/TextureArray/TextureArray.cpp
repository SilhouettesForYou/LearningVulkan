#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <time.h>
#include <vector>

#define GLM_FORCE_RADIANS
#define GLM_FORCE_DEPTH_ZERO_TO_ONE
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

#include <vulkan/vulkan.h>
#include "VulkanBase.h"
#include "VulkanTexture.hpp"
#include "VulkanBuffer.hpp"
#include <../ktx/ktx.h>
#include <../ktx/ktxvulkan.h>

#define ENABLE_VALIDATION false

// Vertex layout for this example
struct Vertex 
{
	float pos[3];
	float uv[2];
};

class VulkanExampleTextureArray : public VulkanBase
{
public:
	// Number of array layers in texture array
	// Also used as instance count
	uint32_t layerCount;
	vks::Texture textureArray;

	vks::Buffer vertexBuffer;
	vks::Buffer indexBuffer;
	uint32_t indexCount;

	vks::Buffer uniformBufferVS;

	struct UboInstanceData {
		// Model matrix
		glm::mat4 model;
		// Texture array index
		// Vec4 due to padding
		glm::vec4 arrayIndex;
	};

	struct {
		// Global matrices
		struct {
			glm::mat4 projection;
			glm::mat4 view;
		} matrices;
		// Seperate data for each instance
		UboInstanceData* instance;
	} uboVS;


	VkPipeline pipeline;
	VkPipelineLayout pipelineLayout;
	VkDescriptorSet descriptorSet;
	VkDescriptorSetLayout descriptorSetLayout;

	VulkanExampleTextureArray() : VulkanBase(ENABLE_VALIDATION)
	{
		title = "Texture arrays";
		settings.overlay = true;
		camera.type = Camera::CameraType::lookat;
		camera.SetPosition(glm::vec3(0.0f, 0.0f, -7.5f));
		camera.SetRotation(glm::vec3(-35.0f, 0.0f, 0.0f));
		camera.SetPerspective(45.0f, (float)width / (float)height, 0.1f, 256.0f);
	}

	~VulkanExampleTextureArray()
	{
		// Clean up used Vulkan resources
		// Note : Inherited destructor cleans up resources stored in base class

		vkDestroyImageView(device, textureArray.view, nullptr);
		vkDestroyImage(device, textureArray.image, nullptr);
		vkDestroySampler(device, textureArray.sampler, nullptr);
		vkFreeMemory(device, textureArray.deviceMemory, nullptr);

		vkDestroyPipeline(device, pipeline, nullptr);

		vkDestroyPipelineLayout(device, pipelineLayout, nullptr);
		vkDestroyDescriptorSetLayout(device, descriptorSetLayout, nullptr);

		vertexBuffer.Destroy();
		indexBuffer.Destroy();

		uniformBufferVS.Destroy();

		delete[] uboVS.instance;
	}

	void LoadTextureArray(std::string filename, VkFormat format)
	{
		ktxResult result;
		ktxTexture* ktxTexture;
#if defined(__ANDROID__)
		// Textures are stored inside the apk on Android (compressed)
		// So they need to be loaded via the asset manager
		AAsset* asset = AAssetManager_open(androidApp->activity->assetManager, filename.c_str(), AASSET_MODE_STREAMING);
		if (!asset) {
			vks::tools::exitFatal("Could not load texture from " + filename + "\n\nThe file may be part of the additional asset pack.\n\nRun \"download_assets.py\" in the repository root to download the latest version.", -1);
		}
		size_t size = AAsset_getLength(asset);
		assert(size > 0);

		ktx_uint8_t* textureData = new ktx_uint8_t[size];
		AAsset_read(asset, textureData, size);
		AAsset_close(asset);
		result = ktxTexture_CreateFromMemory(textureData, size, KTX_TEXTURE_CREATE_LOAD_IMAGE_DATA_BIT, &ktxTexture);
		delete[] textureData;
#else
		if (!vks::tools::FileExists(filename))
		{
			vks::tools::ExitFatal("Could not load texture from " + filename + "\n\nThe file may be part of the additional asset pack.\n\nRun \"download_assets.py\" in the repository root to download the latest version.", -1);
		}

		result = ktxTexture_CreateFromNamedFile(filename.c_str(), KTX_TEXTURE_CREATE_LOAD_IMAGE_DATA_BIT, &ktxTexture);
#endif
		assert(result == KTX_SUCCESS);

		// Get properties required for using and upload texture data from the ktx texture object
		textureArray.width = ktxTexture->baseWidth;
		textureArray.height = ktxTexture->baseHeight;
		layerCount = ktxTexture->numLayers;
		auto ktxTextureData = ktxTexture_GetData(ktxTexture);
		auto ktxTextureSize = ktxTexture_GetSize(ktxTexture);

		VkMemoryAllocateInfo memAllocInfo = vks::initializers::MemoryAllocateInfo();
		VkMemoryRequirements memReqs;

		// Create a host-visible staging buffer that contains the raw image data
		VkBuffer stagingBuffer;
		VkDeviceMemory stagingMemory;

		VkBufferCreateInfo bufferCreateInfo = vks::initializers::BufferCreateInfo();
		bufferCreateInfo.size = ktxTextureSize;
		// This buffer is used as a transfer source for the buffer copy
		bufferCreateInfo.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
		bufferCreateInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

		VK_CHECK_RESULT(vkCreateBuffer(device, &bufferCreateInfo, nullptr, &stagingBuffer));

		// Get memory requirements for the staging buffer (aligment, memory type bits)
		vkGetBufferMemoryRequirements(device, stagingBuffer, &memReqs);

		memAllocInfo.allocationSize = memReqs.size;
		// Get memory type index for a host visible buffer
		memAllocInfo.memoryTypeIndex = vulkanDevice->GetMemoryType(memReqs.memoryTypeBits, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT);

		VK_CHECK_RESULT(vkAllocateMemory(device, &memAllocInfo, nullptr, &stagingMemory));
		VK_CHECK_RESULT(vkBindBufferMemory(device, stagingBuffer, stagingMemory, 0));

		// Copy texture data into staging buffer
		uint8_t* data;
		VK_CHECK_RESULT(vkMapMemory(device, stagingMemory, 0, memReqs.size, 0, reinterpret_cast<void **>(&data)));
		memcpy(data, ktxTextureData, ktxTextureSize);
		vkUnmapMemory(device, stagingMemory);

		// Setup buffer copy regions for ayyay layers
		std::vector<VkBufferImageCopy> bufferCopyRegions;

		// To keep this simple, we will only load layers and no mip level
		for (uint32_t layer = 0; layer < layerCount; layer++)
		{
			// Calculate offset into staging buffer for the current array layer
			ktx_size_t offset;
			KTX_error_code ret = ktxTexture_GetImageOffset(ktxTexture, 0, layer, 0, &offset);
			assert(ret == KTX_SUCCESS);

			// Setup a buffer image copy structure for the current array layer
			VkBufferImageCopy bufferCopyRegion = {};
			bufferCopyRegion.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
			bufferCopyRegion.imageSubresource.mipLevel = 0;
			bufferCopyRegion.imageSubresource.baseArrayLayer = 1;
			bufferCopyRegion.imageSubresource.layerCount = 1;
			bufferCopyRegion.imageExtent.width = ktxTexture->baseWidth;
			bufferCopyRegion.imageExtent.height = ktxTexture->baseHeight;
			bufferCopyRegion.bufferOffset = offset;
			bufferCopyRegions.emplace_back(bufferCopyRegion);
		}

		// Create optimal tiled target image;
		VkImageCreateInfo imageCreateInfo = vks::initializers::ImageCreateInfo();
		imageCreateInfo.imageType = VK_IMAGE_TYPE_2D;
		imageCreateInfo.format = format;
		imageCreateInfo.mipLevels = 1;
		imageCreateInfo.samples = VK_SAMPLE_COUNT_1_BIT;
		imageCreateInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
		imageCreateInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
		imageCreateInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
		imageCreateInfo.extent = { textureArray.width, textureArray.height, 1 };
		imageCreateInfo.usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
		imageCreateInfo.arrayLayers = layerCount;

		VK_CHECK_RESULT(vkCreateImage(device, &imageCreateInfo, nullptr, &textureArray.image));

		vkGetImageMemoryRequirements(device, textureArray.image, &memReqs);

		memAllocInfo.allocationSize = memReqs.size;
		memAllocInfo.memoryTypeIndex = vulkanDevice->GetMemoryType(memReqs.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);

		VK_CHECK_RESULT(vkAllocateMemory(device, &memAllocInfo, nullptr, &textureArray.deviceMemory));
		VK_CHECK_RESULT(vkBindImageMemory(device, textureArray.image, textureArray.deviceMemory, 0));

		VkCommandBuffer copyCmd = vulkanDevice->CreateCommandBuffer(VK_COMMAND_BUFFER_LEVEL_PRIMARY, true);

		// Image barrier for optimal image (target)
		// Set initial layout for all array layers (faces) of the optimal (targer) tiled texture
		VkImageSubresourceRange subresourceRange = {};
		subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
		subresourceRange.baseMipLevel = 0;
		subresourceRange.levelCount = 1;
		subresourceRange.layerCount = layerCount;

		vks::tools::SetImageLayout(copyCmd, textureArray.image, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, subresourceRange);

		// Copy the cube map faces from the staging buffer to the optimal tiled image
		vkCmdCopyBufferToImage(
			copyCmd,
			stagingBuffer,
			textureArray.image,
			VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
			bufferCopyRegions.size(),
			bufferCopyRegions.data()
		);

		// Change texture image layout to shader read after all faces have been copied
		textureArray.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
		vks::tools::SetImageLayout(
			copyCmd,
			textureArray.image,
			VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
			textureArray.imageLayout,
			subresourceRange
		);

		vulkanDevice->FlushCommandBuffer(copyCmd, queue, true);

		// Create sampler
		VkSamplerCreateInfo sampler = vks::initializers::SamplerCreateInfo();
		sampler.magFilter = VK_FILTER_LINEAR;
		sampler.minFilter = VK_FILTER_LINEAR;
		sampler.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
		sampler.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
		sampler.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
		sampler.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
		sampler.mipLodBias = 0.0f;
		sampler.maxAnisotropy = 8;
		sampler.compareOp = VK_COMPARE_OP_NEVER;
		sampler.minLod = 0.0f;
		sampler.maxLod = 0.0f;
		sampler.borderColor = VK_BORDER_COLOR_FLOAT_OPAQUE_WHITE;
		VK_CHECK_RESULT(vkCreateSampler(device, &sampler, nullptr, &textureArray.sampler));

		// Create image view
		VkImageViewCreateInfo view = vks::initializers::ImageViewCreateInfo();
		view.viewType = VK_IMAGE_VIEW_TYPE_2D_ARRAY;
		view.format = format;
		view.components = { VK_COMPONENT_SWIZZLE_R, VK_COMPONENT_SWIZZLE_G, VK_COMPONENT_SWIZZLE_B, VK_COMPONENT_SWIZZLE_A };
		view.subresourceRange.layerCount = layerCount;
		view.subresourceRange.levelCount = 1;
		view.image = textureArray.image;
		VK_CHECK_RESULT(vkCreateImageView(device, &view, nullptr, &textureArray.view));

		// Clean up staging resources
		vkFreeMemory(device, stagingMemory, nullptr);
		vkDestroyBuffer(device, stagingBuffer, nullptr);
		ktxTexture_Destroy(ktxTexture);
	}

	void LoadTextures()
	{
		// Vulkan core supports three different compressed texture formats
		// As the support differs between implemementations we need to check device features and select a proper format and file
		std::string filename;
		VkFormat format;
		if (deviceFeatures.textureCompressionBC) {
			filename = "texturearray_bc3_unorm.ktx";
			format = VK_FORMAT_BC3_UNORM_BLOCK;
		}
		else if (deviceFeatures.textureCompressionASTC_LDR) {
			filename = "texturearray_astc_8x8_unorm.ktx";
			format = VK_FORMAT_ASTC_8x8_UNORM_BLOCK;
		}
		else if (deviceFeatures.textureCompressionETC2) {
			filename = "texturearray_etc2_unorm.ktx";
			format = VK_FORMAT_ETC2_R8G8B8_UNORM_BLOCK;
		}
		else {
			vks::tools::ExitFatal("Device does not support any compressed texture format!", VK_ERROR_FEATURE_NOT_PRESENT);
		}
		LoadTextureArray(GetAssetPath() + "textures/" + filename, format);
	}

	void BuildCommandBuffers()
	{
		VkCommandBufferBeginInfo cmdBufInfo = vks::initializers::CommandBufferBeginInfo();

		VkClearValue clearValues[2];
		clearValues[0].color = defaultClearColor;
		clearValues[1].depthStencil = { 1.0f, 0 };

		VkRenderPassBeginInfo renderPassBeginInfo = vks::initializers::RenderPassBeginInfo();
		renderPassBeginInfo.renderPass = renderPass;
		renderPassBeginInfo.renderArea.offset.x = 0;
		renderPassBeginInfo.renderArea.offset.y = 0;
		renderPassBeginInfo.renderArea.extent.width = width;
		renderPassBeginInfo.renderArea.extent.height = height;
		renderPassBeginInfo.clearValueCount = 2;
		renderPassBeginInfo.pClearValues = clearValues;

		for (int32_t i = 0; i < drawCmdBuffers.size(); ++i)
		{
			// Set target frame buffer
			renderPassBeginInfo.framebuffer = frameBuffers[i];

			VK_CHECK_RESULT(vkBeginCommandBuffer(drawCmdBuffers[i], &cmdBufInfo));

			vkCmdBeginRenderPass(drawCmdBuffers[i], &renderPassBeginInfo, VK_SUBPASS_CONTENTS_INLINE);

			VkViewport viewport = vks::initializers::Viewport((float)width, (float)height, 0.0f, 1.0f);
			vkCmdSetViewport(drawCmdBuffers[i], 0, 1, &viewport);

			VkRect2D scissor = vks::initializers::Rect2D(width, height, 0, 0);
			vkCmdSetScissor(drawCmdBuffers[i], 0, 1, &scissor);

			vkCmdBindDescriptorSets(drawCmdBuffers[i], VK_PIPELINE_BIND_POINT_GRAPHICS, pipelineLayout, 0, 1, &descriptorSet, 0, NULL);
			vkCmdBindPipeline(drawCmdBuffers[i], VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);

			VkDeviceSize offsets[1] = { 0 };
			vkCmdBindVertexBuffers(drawCmdBuffers[i], 0, 1, &vertexBuffer.buffer, offsets);
			vkCmdBindIndexBuffer(drawCmdBuffers[i], indexBuffer.buffer, 0, VK_INDEX_TYPE_UINT32);

			vkCmdDrawIndexed(drawCmdBuffers[i], indexCount, layerCount, 0, 0, 0);

			DrawUI(drawCmdBuffers[i]);

			vkCmdEndRenderPass(drawCmdBuffers[i]);

			VK_CHECK_RESULT(vkEndCommandBuffer(drawCmdBuffers[i]));
		}
	}

	void GenerateCube()
	{
		std::vector<Vertex> vertices = {
			{ { -1.0f, -1.0f,  1.0f }, { 0.0f, 0.0f } },
			{ {  1.0f, -1.0f,  1.0f }, { 1.0f, 0.0f } },
			{ {  1.0f,  1.0f,  1.0f }, { 1.0f, 1.0f } },
			{ { -1.0f,  1.0f,  1.0f }, { 0.0f, 1.0f } },

			{ {  1.0f,  1.0f,  1.0f }, { 0.0f, 0.0f } },
			{ {  1.0f,  1.0f, -1.0f }, { 1.0f, 0.0f } },
			{ {  1.0f, -1.0f, -1.0f }, { 1.0f, 1.0f } },
			{ {  1.0f, -1.0f,  1.0f }, { 0.0f, 1.0f } },

			{ { -1.0f, -1.0f, -1.0f }, { 0.0f, 0.0f } },
			{ {  1.0f, -1.0f, -1.0f }, { 1.0f, 0.0f } },
			{ {  1.0f,  1.0f, -1.0f }, { 1.0f, 1.0f } },
			{ { -1.0f,  1.0f, -1.0f }, { 0.0f, 1.0f } },

			{ { -1.0f, -1.0f, -1.0f }, { 0.0f, 0.0f } },
			{ { -1.0f, -1.0f,  1.0f }, { 1.0f, 0.0f } },
			{ { -1.0f,  1.0f,  1.0f }, { 1.0f, 1.0f } },
			{ { -1.0f,  1.0f, -1.0f }, { 0.0f, 1.0f } },

			{ {  1.0f,  1.0f,  1.0f }, { 0.0f, 0.0f } },
			{ { -1.0f,  1.0f,  1.0f }, { 1.0f, 0.0f } },
			{ { -1.0f,  1.0f, -1.0f }, { 1.0f, 1.0f } },
			{ {  1.0f,  1.0f, -1.0f }, { 0.0f, 1.0f } },

			{ { -1.0f, -1.0f, -1.0f }, { 0.0f, 0.0f } },
			{ {  1.0f, -1.0f, -1.0f }, { 1.0f, 0.0f } },
			{ {  1.0f, -1.0f,  1.0f }, { 1.0f, 1.0f } },
			{ { -1.0f, -1.0f,  1.0f }, { 0.0f, 1.0f } },
		};
		std::vector<uint32_t> indices = {
			0,1,2, 0,2,3, 4,5,6,  4,6,7, 8,9,10, 8,10,11, 12,13,14, 12,14,15, 16,17,18, 16,18,19, 20,21,22, 20,22,23
		};

		indexCount = static_cast<uint32_t>(indices.size());

		// Create buffers
		// For the sake of simplicity we won't stage the vertex data to the gpu memory
		VK_CHECK_RESULT(vulkanDevice->CreateBuffer(
			VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
			VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
			&vertexBuffer,
			vertices.size() * sizeof(Vertex),
			vertices.data()));
		VK_CHECK_RESULT(vulkanDevice->CreateBuffer(
			VK_BUFFER_USAGE_INDEX_BUFFER_BIT,
			VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
			&indexBuffer,
			indices.size() * sizeof(uint32_t),
			indices.data()));
	}

	void SetupDescriptorPool()
	{
		std::vector<VkDescriptorPoolSize> poolSizes = {
			vks::initializers::DescriptorPoolSize(VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1),
			vks::initializers::DescriptorPoolSize(VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1)
		};
		VkDescriptorPoolCreateInfo descriptorPoolInfo = vks::initializers::DescriptorPoolCreateInfo(poolSizes.size(), poolSizes.data(), 2);
		VK_CHECK_RESULT(vkCreateDescriptorPool(device, &descriptorPoolInfo, nullptr, &descriptorPool));
	}

	void SetupDescriptorSetLayout()
	{
		std::vector<VkDescriptorSetLayoutBinding> setLayoutBindings = {
			// Binding 0 : Vertex shader uniform buffer
			vks::initializers::DescriptorSetLayoutBinding(VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, VK_SHADER_STAGE_VERTEX_BIT, 0),
			// Binding 1 : Fragment shader image sampler (texture array)
			vks::initializers::DescriptorSetLayoutBinding(VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_FRAGMENT_BIT, 1)
		};
		VkDescriptorSetLayoutCreateInfo descriptorLayout = vks::initializers::DescriptorSetLayoutCreateInfo(setLayoutBindings.data(), setLayoutBindings.size());
		VK_CHECK_RESULT(vkCreateDescriptorSetLayout(device, &descriptorLayout, nullptr, &descriptorSetLayout));

		VkPipelineLayoutCreateInfo pipelineLayoutCreateInfo = vks::initializers::PipelineLayoutCreateInfo(&descriptorSetLayout, 1);
		VK_CHECK_RESULT(vkCreatePipelineLayout(device, &pipelineLayoutCreateInfo, nullptr, &pipelineLayout));
	}

	void SetupDescriptorSet()
	{
		VkDescriptorSetAllocateInfo allocInfo = vks::initializers::DescriptorSetAllocateInfo(descriptorPool, &descriptorSetLayout, 1);
		VK_CHECK_RESULT(vkAllocateDescriptorSets(device, &allocInfo, &descriptorSet));

		// Image descriptor for the texture array
		VkDescriptorImageInfo textureDescriptor =
			vks::initializers::DescriptorImageInfo(
				textureArray.sampler,
				textureArray.view,
				textureArray.imageLayout);

		std::vector<VkWriteDescriptorSet> writeDescriptorSets = {
			// Binding 0 : Vertex shader uniform buffer
			vks::initializers::WriteDescriptorSet(descriptorSet, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 0, &uniformBufferVS.descriptor),
			// Binding 1 : Fragment shader cubemap sampler
			vks::initializers::WriteDescriptorSet(descriptorSet, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, &textureDescriptor)
		};
		vkUpdateDescriptorSets(device, writeDescriptorSets.size(), writeDescriptorSets.data(), 0, NULL);
	}

	void PreparePipelines()
	{
		VkPipelineInputAssemblyStateCreateInfo inputAssemblyStateCI = vks::initializers::PipelineInputAssemblyStateCreateInfo(VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST, 0, VK_FALSE);
		VkPipelineRasterizationStateCreateInfo rasterizationStateCI = vks::initializers::PipelineRasterizationStateCreateInfo(VK_POLYGON_MODE_FILL, VK_CULL_MODE_NONE, VK_FRONT_FACE_COUNTER_CLOCKWISE, 0);
		VkPipelineColorBlendAttachmentState blendAttachmentState	= vks::initializers::PipelineColorBlendAttachmentState(0xf, VK_FALSE);
		VkPipelineColorBlendStateCreateInfo colorBlendStateCI		= vks::initializers::PipelineColorBlendStateCreateInfo(1, &blendAttachmentState);
		VkPipelineDepthStencilStateCreateInfo depthStencilStateCI   = vks::initializers::PipelineDepthStencilStateCreateInfo(VK_TRUE, VK_TRUE, VK_COMPARE_OP_LESS_OR_EQUAL);
		VkPipelineViewportStateCreateInfo viewportStateCI			= vks::initializers::PipelineViewportStateCreateInfo(1, 1, 0);
		VkPipelineMultisampleStateCreateInfo multisampleStateCI		= vks::initializers::PipelineMultisampleStateCreateInfo(VK_SAMPLE_COUNT_1_BIT, 0);
		std::vector<VkDynamicState> dynamicStateEnables				= { VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR };
		VkPipelineDynamicStateCreateInfo dynamicStateCI				= vks::initializers::PipelineDynamicStateCreateInfo(dynamicStateEnables.data(), dynamicStateEnables.size(), 0);

		// Vertex bindings and attributes
		VkVertexInputBindingDescription vertexInputBinding = { 0, sizeof(Vertex), VK_VERTEX_INPUT_RATE_VERTEX };
		std::vector<VkVertexInputAttributeDescription> vertexInputAttributes = {
			{ 0, 0, VK_FORMAT_R32G32B32_SFLOAT, offsetof(Vertex, pos) },
			{ 1, 0, VK_FORMAT_R32G32_SFLOAT, offsetof(Vertex, uv) },
		};
		VkPipelineVertexInputStateCreateInfo vertexInputStateCI = vks::initializers::PipelineVertexInputStateCreateInfo();
		vertexInputStateCI.vertexBindingDescriptionCount = 1;
		vertexInputStateCI.pVertexBindingDescriptions = &vertexInputBinding;
		vertexInputStateCI.vertexAttributeDescriptionCount = static_cast<uint32_t>(vertexInputAttributes.size());
		vertexInputStateCI.pVertexAttributeDescriptions = vertexInputAttributes.data();

		// Instacing pipeline
		std::array<VkPipelineShaderStageCreateInfo, 2> shaderStages;

		shaderStages[0] = LoadShader(GetAssetPath() + "shaders/texturearray/instancing.vert.spv", VK_SHADER_STAGE_VERTEX_BIT);
		shaderStages[1] = LoadShader(GetAssetPath() + "shaders/texturearray/instancing.frag.spv", VK_SHADER_STAGE_FRAGMENT_BIT);

		VkGraphicsPipelineCreateInfo pipelineCI = vks::initializers::PipelineCreateInfo(pipelineLayout, renderPass, 0);
		pipelineCI.pVertexInputState = &vertexInputStateCI;
		pipelineCI.pInputAssemblyState = &inputAssemblyStateCI;
		pipelineCI.pRasterizationState = &rasterizationStateCI;
		pipelineCI.pColorBlendState = &colorBlendStateCI;
		pipelineCI.pMultisampleState = &multisampleStateCI;
		pipelineCI.pViewportState = &viewportStateCI;
		pipelineCI.pDepthStencilState = &depthStencilStateCI;
		pipelineCI.pDynamicState = &dynamicStateCI;
		pipelineCI.stageCount = shaderStages.size();
		pipelineCI.pStages = shaderStages.data();

		VK_CHECK_RESULT(vkCreateGraphicsPipelines(device, pipelineCache, 1, &pipelineCI, nullptr, &pipeline));
	}

	void PrepareUniformBuffers()
	{
		uboVS.instance = new UboInstanceData[layerCount];

		uint32_t uboSize = sizeof(uboVS.matrices) + (layerCount * sizeof(UboInstanceData));

		// Vertex shader uniform buffer block
		VK_CHECK_RESULT(vulkanDevice->CreateBuffer(
			VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
			VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
			&uniformBufferVS,
			uboSize));

		// Array indices and model matrices are fixed
		float offset = -1.5f;
		float center = (layerCount * offset) / 2.0f - (offset * 0.5f);
		for (uint32_t i = 0; i < layerCount; i++) {
			// Instance model matrix
			uboVS.instance[i].model = glm::translate(glm::mat4(1.0f), glm::vec3(i * offset - center, 0.0f, 0.0f));
			uboVS.instance[i].model = glm::scale(uboVS.instance[i].model, glm::vec3(0.5f));
			// Instance texture array index
			uboVS.instance[i].arrayIndex.x = (float)i;
		}

		// Update instanced part of the uniform buffer
		uint8_t* pData;
		uint32_t dataOffset = sizeof(uboVS.matrices);
		uint32_t dataSize = layerCount * sizeof(UboInstanceData);
		VK_CHECK_RESULT(vkMapMemory(device, uniformBufferVS.memory, dataOffset, dataSize, 0, (void**)&pData));
		memcpy(pData, uboVS.instance, dataSize);
		vkUnmapMemory(device, uniformBufferVS.memory);

		// Map persistent
		VK_CHECK_RESULT(uniformBufferVS.Map());

		UpdateUniformBuffersCamera();
	}

	void UpdateUniformBuffersCamera()
	{
		uboVS.matrices.projection = camera.matrices.perspective;
		uboVS.matrices.view = camera.matrices.view;
		memcpy(uniformBufferVS.mapped, &uboVS.matrices, sizeof(uboVS.matrices));
	}

	void Draw()
	{
		__super::PrepareFrame();

		submitInfo.commandBufferCount = 1;
		submitInfo.pCommandBuffers = &drawCmdBuffers[currentBuffer];
		VK_CHECK_RESULT(vkQueueSubmit(queue, 1, &submitInfo, VK_NULL_HANDLE));

		__super::SubmitFrame();
	}

	void Prepare()
	{
		__super::Prepare();
		LoadTextures();
		GenerateCube();
		PrepareUniformBuffers();
		SetupDescriptorSetLayout();
		PreparePipelines();
		SetupDescriptorPool();
		SetupDescriptorSet();
		BuildCommandBuffers();
		prepared = true;
	}

	virtual void Render()
	{
		if (!prepared)
			return;
		Draw();
		if (camera.updated)
			UpdateUniformBuffersCamera();
	}

};

VULKAN_EXAMPLE_MAIN(VulkanExampleTextureArray)