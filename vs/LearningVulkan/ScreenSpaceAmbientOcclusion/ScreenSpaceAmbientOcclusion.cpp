#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <vector>
#include <random>

#define GLM_FORCE_RADIANS
#define GLM_FORCE_DEPTH_ZERO_TO_ONE
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

#include <vulkan/vulkan.h>
#include "VulkanBase.h"
#include "VulkanTexture.hpp"
#include "VulkanModel.hpp"

#define ENABLE_VALIDATION false

#define SSAO_KERNEL_SIZE 32
#define SSAO_RADIUS 0.5f

#if defined(__ANDROID__)
#define SSAO_NOISE_DIM 8
#else
#define SSAO_NOISE_DIM 4
#endif

class VulkanExampleSSAO : public VulkanBase
{
public:
	struct {
		vks::Texture2D ssaoNoise;
	} textures;

	// Vertex layout for the models
	vks::VertexLayout vertexLayout = vks::VertexLayout({
		vks::VERTEX_COMPONENT_POSITION,
		vks::VERTEX_COMPONENT_UV,
		vks::VERTEX_COMPONENT_COLOR,
		vks::VERTEX_COMPONENT_NORMAL,
		});

	struct {
		vks::Model scene;
	} models;

	struct UBOSceneMatrices {
		glm::mat4 projection;
		glm::mat4 model;
		glm::mat4 view;
	} uboSceneMatrices;

	struct UBOSSAOParams {
		glm::mat4 projection;
		int32_t ssao = true;
		int32_t ssaoOnly = false;
		int32_t ssaoBlur = true;
	} uboSSAOParams;

	struct {
		VkPipeline offscreen;
		VkPipeline composition;
		VkPipeline ssao;
		VkPipeline ssaoBlur;
	} pipelines;

	struct {
		VkPipelineLayout gBuffer;
		VkPipelineLayout ssao;
		VkPipelineLayout ssaoBlur;
		VkPipelineLayout composition;
	} pipelineLayouts;

	struct {
		const uint32_t count = 5;
		VkDescriptorSet model;
		VkDescriptorSet floor;
		VkDescriptorSet ssao;
		VkDescriptorSet ssaoBlur;
		VkDescriptorSet composition;
	} descriptorSets;

	struct {
		VkDescriptorSetLayout gBuffer;
		VkDescriptorSetLayout ssao;
		VkDescriptorSetLayout ssaoBlur;
		VkDescriptorSetLayout composition;
	} descriptorSetLayouts;

	struct {
		vks::Buffer sceneMatrices;
		vks::Buffer ssaoKernel;
		vks::Buffer ssaoParams;
	} uniformBuffers;

	// Framebuffer for offscreen rendering
	struct FrameBufferAttachment {
		VkImage image;
		VkDeviceMemory mem;
		VkImageView view;
		VkFormat format;
		void destroy(VkDevice device)
		{
			vkDestroyImage(device, image, nullptr);
			vkDestroyImageView(device, view, nullptr);
			vkFreeMemory(device, mem, nullptr);
		}
	};
	struct FrameBuffer {
		int32_t width, height;
		VkFramebuffer frameBuffer;
		VkRenderPass renderPass;
		void setSize(int32_t w, int32_t h)
		{
			this->width = w;
			this->height = h;
		}
		void destroy(VkDevice device)
		{
			vkDestroyFramebuffer(device, frameBuffer, nullptr);
			vkDestroyRenderPass(device, renderPass, nullptr);
		}
	};

	struct {
		struct Offscreen : public FrameBuffer {
			FrameBufferAttachment position, normal, albedo, depth;
		} offscreen;
		struct SSAO : public FrameBuffer {
			FrameBufferAttachment color;
		} ssao, ssaoBlur;
	} frameBuffers;

	// One sampler for the frame buffer color attachments
	VkSampler colorSampler;

	VulkanExampleSSAO() : VulkanBase(ENABLE_VALIDATION)
	{
		title = "Screen space ambient occlusion";
		settings.overlay = true;
		camera.type = Camera::CameraType::firstperson;
		camera.movementSpeed = 5.0f;
#ifndef __ANDROID__
		camera.rotationSpeed = 0.25f;
#endif
		camera.position = { 7.5f, -6.75f, 0.0f };
		camera.SetRotation(glm::vec3(5.0f, 90.0f, 0.0f));
		camera.SetPerspective(60.0f, (float)width / (float)height, 0.1f, 64.0f);
	}

	~VulkanExampleSSAO()
	{
		vkDestroySampler(device, colorSampler, nullptr);

		// Attachments
		frameBuffers.offscreen.position.destroy(device);
		frameBuffers.offscreen.normal.destroy(device);
		frameBuffers.offscreen.albedo.destroy(device);
		frameBuffers.offscreen.depth.destroy(device);
		frameBuffers.ssao.color.destroy(device);
		frameBuffers.ssaoBlur.color.destroy(device);

		// Framebuffers
		frameBuffers.offscreen.destroy(device);
		frameBuffers.ssao.destroy(device);
		frameBuffers.ssaoBlur.destroy(device);

		vkDestroyPipeline(device, pipelines.offscreen, nullptr);
		vkDestroyPipeline(device, pipelines.composition, nullptr);
		vkDestroyPipeline(device, pipelines.ssao, nullptr);
		vkDestroyPipeline(device, pipelines.ssaoBlur, nullptr);

		vkDestroyPipelineLayout(device, pipelineLayouts.gBuffer, nullptr);
		vkDestroyPipelineLayout(device, pipelineLayouts.ssao, nullptr);
		vkDestroyPipelineLayout(device, pipelineLayouts.ssaoBlur, nullptr);
		vkDestroyPipelineLayout(device, pipelineLayouts.composition, nullptr);

		vkDestroyDescriptorSetLayout(device, descriptorSetLayouts.gBuffer, nullptr);
		vkDestroyDescriptorSetLayout(device, descriptorSetLayouts.ssao, nullptr);
		vkDestroyDescriptorSetLayout(device, descriptorSetLayouts.ssaoBlur, nullptr);
		vkDestroyDescriptorSetLayout(device, descriptorSetLayouts.composition, nullptr);

		// Meshes
		models.scene.Destroy();

		// Uniform buffers
		uniformBuffers.sceneMatrices.Destroy();
		uniformBuffers.ssaoKernel.Destroy();
		uniformBuffers.ssaoParams.Destroy();

		textures.ssaoNoise.Destroy();
	}

	// Create a frame buffer attachment
	void CreateAttachment(
		VkFormat format,
		VkImageUsageFlagBits usage,
		FrameBufferAttachment* attachment,
		uint32_t width,
		uint32_t height)
	{
		VkImageAspectFlags aspectMask = 0;
		VkImageLayout imageLayout;

		attachment->format = format;

		if (usage & VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT)
		{
			aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
			imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
		}
		if (usage & VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT)
		{
			aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT | VK_IMAGE_ASPECT_STENCIL_BIT;
			imageLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
		}

		assert(aspectMask > 0);

		VkImageCreateInfo image = vks::initializers::ImageCreateInfo();
		image.imageType = VK_IMAGE_TYPE_2D;
		image.format = format;
		image.extent.width = width;
		image.extent.height = height;
		image.extent.depth = 1;
		image.mipLevels = 1;
		image.arrayLayers = 1;
		image.samples = VK_SAMPLE_COUNT_1_BIT;
		image.tiling = VK_IMAGE_TILING_OPTIMAL;
		image.usage = usage | VK_IMAGE_USAGE_SAMPLED_BIT;

		VkMemoryAllocateInfo memAlloc = vks::initializers::MemoryAllocateInfo();
		VkMemoryRequirements memReqs;

		VK_CHECK_RESULT(vkCreateImage(device, &image, nullptr, &attachment->image));
		vkGetImageMemoryRequirements(device, attachment->image, &memReqs);
		memAlloc.allocationSize = memReqs.size;
		memAlloc.memoryTypeIndex = vulkanDevice->GetMemoryType(memReqs.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
		VK_CHECK_RESULT(vkAllocateMemory(device, &memAlloc, nullptr, &attachment->mem));
		VK_CHECK_RESULT(vkBindImageMemory(device, attachment->image, attachment->mem, 0));

		VkImageViewCreateInfo imageView = vks::initializers::ImageViewCreateInfo();
		imageView.viewType = VK_IMAGE_VIEW_TYPE_2D;
		imageView.format = format;
		imageView.subresourceRange = {};
		imageView.subresourceRange.aspectMask = aspectMask;
		imageView.subresourceRange.baseMipLevel = 0;
		imageView.subresourceRange.levelCount = 1;
		imageView.subresourceRange.baseArrayLayer = 0;
		imageView.subresourceRange.layerCount = 1;
		imageView.image = attachment->image;
		VK_CHECK_RESULT(vkCreateImageView(device, &imageView, nullptr, &attachment->view));
	}

	void PrepareOffscreenFramebuffers()
	{
		// Attachments
#if defined(__ANDROID__)
		const uint32_t ssaoWidth = width / 2;
		const uint32_t ssaoHeight = height / 2;
#else
		const uint32_t ssaoWidth = width;
		const uint32_t ssaoHeight = height;
#endif

		frameBuffers.offscreen.setSize(width, height);
		frameBuffers.ssao.setSize(ssaoWidth, ssaoHeight);
		frameBuffers.ssaoBlur.setSize(width, height);

		// Find a suitable depth format
		VkFormat attDepthFormat;
		VkBool32 validDepthFormat = vks::tools::GetSupportedDepthFormat(physicalDevice, &attDepthFormat);
		assert(validDepthFormat);

		// G-Buffer
		CreateAttachment(VK_FORMAT_R32G32B32A32_SFLOAT, VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT, &frameBuffers.offscreen.position, width, height);	// Position + Depth
		CreateAttachment(VK_FORMAT_R8G8B8A8_UNORM, VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT, &frameBuffers.offscreen.normal, width, height);			// Normals
		CreateAttachment(VK_FORMAT_R8G8B8A8_UNORM, VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT, &frameBuffers.offscreen.albedo, width, height);			// Albedo (color)
		CreateAttachment(attDepthFormat, VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT, &frameBuffers.offscreen.depth, width, height);			// Depth

		// SSAO
		CreateAttachment(VK_FORMAT_R8_UNORM, VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT, &frameBuffers.ssao.color, ssaoWidth, ssaoHeight);				// Color

		// SSAO blur
		CreateAttachment(VK_FORMAT_R8_UNORM, VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT, &frameBuffers.ssaoBlur.color, width, height);					// Color

		// Render passes

		// G-Buffer creation
		{
			std::array<VkAttachmentDescription, 4> attachmentDescs = {};

			// Init attachment properties
			for (uint32_t i = 0; i < static_cast<uint32_t>(attachmentDescs.size()); i++)
			{
				attachmentDescs[i].samples = VK_SAMPLE_COUNT_1_BIT;
				attachmentDescs[i].loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
				attachmentDescs[i].storeOp = VK_ATTACHMENT_STORE_OP_STORE;
				attachmentDescs[i].stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
				attachmentDescs[i].stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
				attachmentDescs[i].finalLayout = (i == 3) ? VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL : VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
			}

			// Formats
			attachmentDescs[0].format = frameBuffers.offscreen.position.format;
			attachmentDescs[1].format = frameBuffers.offscreen.normal.format;
			attachmentDescs[2].format = frameBuffers.offscreen.albedo.format;
			attachmentDescs[3].format = frameBuffers.offscreen.depth.format;

			std::vector<VkAttachmentReference> colorReferences;
			colorReferences.push_back({ 0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL });
			colorReferences.push_back({ 1, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL });
			colorReferences.push_back({ 2, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL });

			VkAttachmentReference depthReference = {};
			depthReference.attachment = 3;
			depthReference.layout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

			VkSubpassDescription subpass = {};
			subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
			subpass.pColorAttachments = colorReferences.data();
			subpass.colorAttachmentCount = static_cast<uint32_t>(colorReferences.size());
			subpass.pDepthStencilAttachment = &depthReference;

			// Use subpass dependencies for attachment layout transitions
			std::array<VkSubpassDependency, 2> dependencies;

			dependencies[0].srcSubpass = VK_SUBPASS_EXTERNAL;
			dependencies[0].dstSubpass = 0;
			dependencies[0].srcStageMask = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
			dependencies[0].dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
			dependencies[0].srcAccessMask = VK_ACCESS_SHADER_READ_BIT;
			dependencies[0].dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
			dependencies[0].dependencyFlags = VK_DEPENDENCY_BY_REGION_BIT;

			dependencies[1].srcSubpass = 0;
			dependencies[1].dstSubpass = VK_SUBPASS_EXTERNAL;
			dependencies[1].srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
			dependencies[1].dstStageMask = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
			dependencies[1].srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
			dependencies[1].dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
			dependencies[1].dependencyFlags = VK_DEPENDENCY_BY_REGION_BIT;

			VkRenderPassCreateInfo renderPassInfo = {};
			renderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
			renderPassInfo.pAttachments = attachmentDescs.data();
			renderPassInfo.attachmentCount = static_cast<uint32_t>(attachmentDescs.size());
			renderPassInfo.subpassCount = 1;
			renderPassInfo.pSubpasses = &subpass;
			renderPassInfo.dependencyCount = 2;
			renderPassInfo.pDependencies = dependencies.data();
			VK_CHECK_RESULT(vkCreateRenderPass(device, &renderPassInfo, nullptr, &frameBuffers.offscreen.renderPass));

			std::array<VkImageView, 4> attachments;
			attachments[0] = frameBuffers.offscreen.position.view;
			attachments[1] = frameBuffers.offscreen.normal.view;
			attachments[2] = frameBuffers.offscreen.albedo.view;
			attachments[3] = frameBuffers.offscreen.depth.view;

			VkFramebufferCreateInfo fbufCreateInfo = vks::initializers::FramebufferCreateInfo();
			fbufCreateInfo.renderPass = frameBuffers.offscreen.renderPass;
			fbufCreateInfo.pAttachments = attachments.data();
			fbufCreateInfo.attachmentCount = static_cast<uint32_t>(attachments.size());
			fbufCreateInfo.width = frameBuffers.offscreen.width;
			fbufCreateInfo.height = frameBuffers.offscreen.height;
			fbufCreateInfo.layers = 1;
			VK_CHECK_RESULT(vkCreateFramebuffer(device, &fbufCreateInfo, nullptr, &frameBuffers.offscreen.frameBuffer));
		}

		// SSAO
		{
			VkAttachmentDescription attachmentDescription{};
			attachmentDescription.format = frameBuffers.ssao.color.format;
			attachmentDescription.samples = VK_SAMPLE_COUNT_1_BIT;
			attachmentDescription.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
			attachmentDescription.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
			attachmentDescription.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
			attachmentDescription.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
			attachmentDescription.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
			attachmentDescription.finalLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

			VkAttachmentReference colorReference = { 0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL };

			VkSubpassDescription subpass = {};
			subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
			subpass.pColorAttachments = &colorReference;
			subpass.colorAttachmentCount = 1;

			std::array<VkSubpassDependency, 2> dependencies;

			dependencies[0].srcSubpass = VK_SUBPASS_EXTERNAL;
			dependencies[0].dstSubpass = 0;
			dependencies[0].srcStageMask = VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT;
			dependencies[0].dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
			dependencies[0].srcAccessMask = VK_ACCESS_MEMORY_READ_BIT;
			dependencies[0].dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
			dependencies[0].dependencyFlags = VK_DEPENDENCY_BY_REGION_BIT;

			dependencies[1].srcSubpass = 0;
			dependencies[1].dstSubpass = VK_SUBPASS_EXTERNAL;
			dependencies[1].srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
			dependencies[1].dstStageMask = VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT;
			dependencies[1].srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
			dependencies[1].dstAccessMask = VK_ACCESS_MEMORY_READ_BIT;
			dependencies[1].dependencyFlags = VK_DEPENDENCY_BY_REGION_BIT;

			VkRenderPassCreateInfo renderPassInfo = {};
			renderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
			renderPassInfo.pAttachments = &attachmentDescription;
			renderPassInfo.attachmentCount = 1;
			renderPassInfo.subpassCount = 1;
			renderPassInfo.pSubpasses = &subpass;
			renderPassInfo.dependencyCount = 2;
			renderPassInfo.pDependencies = dependencies.data();
			VK_CHECK_RESULT(vkCreateRenderPass(device, &renderPassInfo, nullptr, &frameBuffers.ssao.renderPass));

			VkFramebufferCreateInfo fbufCreateInfo = vks::initializers::FramebufferCreateInfo();
			fbufCreateInfo.renderPass = frameBuffers.ssao.renderPass;
			fbufCreateInfo.pAttachments = &frameBuffers.ssao.color.view;
			fbufCreateInfo.attachmentCount = 1;
			fbufCreateInfo.width = frameBuffers.ssao.width;
			fbufCreateInfo.height = frameBuffers.ssao.height;
			fbufCreateInfo.layers = 1;
			VK_CHECK_RESULT(vkCreateFramebuffer(device, &fbufCreateInfo, nullptr, &frameBuffers.ssao.frameBuffer));
		}

		// SSAO Blur
		{
			VkAttachmentDescription attachmentDescription{};
			attachmentDescription.format = frameBuffers.ssaoBlur.color.format;
			attachmentDescription.samples = VK_SAMPLE_COUNT_1_BIT;
			attachmentDescription.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
			attachmentDescription.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
			attachmentDescription.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
			attachmentDescription.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
			attachmentDescription.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
			attachmentDescription.finalLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

			VkAttachmentReference colorReference = { 0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL };

			VkSubpassDescription subpass = {};
			subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
			subpass.pColorAttachments = &colorReference;
			subpass.colorAttachmentCount = 1;

			std::array<VkSubpassDependency, 2> dependencies;

			dependencies[0].srcSubpass = VK_SUBPASS_EXTERNAL;
			dependencies[0].dstSubpass = 0;
			dependencies[0].srcStageMask = VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT;
			dependencies[0].dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
			dependencies[0].srcAccessMask = VK_ACCESS_MEMORY_READ_BIT;
			dependencies[0].dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
			dependencies[0].dependencyFlags = VK_DEPENDENCY_BY_REGION_BIT;

			dependencies[1].srcSubpass = 0;
			dependencies[1].dstSubpass = VK_SUBPASS_EXTERNAL;
			dependencies[1].srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
			dependencies[1].dstStageMask = VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT;
			dependencies[1].srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
			dependencies[1].dstAccessMask = VK_ACCESS_MEMORY_READ_BIT;
			dependencies[1].dependencyFlags = VK_DEPENDENCY_BY_REGION_BIT;

			VkRenderPassCreateInfo renderPassInfo = {};
			renderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
			renderPassInfo.pAttachments = &attachmentDescription;
			renderPassInfo.attachmentCount = 1;
			renderPassInfo.subpassCount = 1;
			renderPassInfo.pSubpasses = &subpass;
			renderPassInfo.dependencyCount = 2;
			renderPassInfo.pDependencies = dependencies.data();
			VK_CHECK_RESULT(vkCreateRenderPass(device, &renderPassInfo, nullptr, &frameBuffers.ssaoBlur.renderPass));

			VkFramebufferCreateInfo fbufCreateInfo = vks::initializers::FramebufferCreateInfo();
			fbufCreateInfo.renderPass = frameBuffers.ssaoBlur.renderPass;
			fbufCreateInfo.pAttachments = &frameBuffers.ssaoBlur.color.view;
			fbufCreateInfo.attachmentCount = 1;
			fbufCreateInfo.width = frameBuffers.ssaoBlur.width;
			fbufCreateInfo.height = frameBuffers.ssaoBlur.height;
			fbufCreateInfo.layers = 1;
			VK_CHECK_RESULT(vkCreateFramebuffer(device, &fbufCreateInfo, nullptr, &frameBuffers.ssaoBlur.frameBuffer));
		}

		// Shared sampler used for all color attachments
		VkSamplerCreateInfo sampler = vks::initializers::SamplerCreateInfo();
		sampler.magFilter = VK_FILTER_NEAREST;
		sampler.minFilter = VK_FILTER_NEAREST;
		sampler.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
		sampler.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
		sampler.addressModeV = sampler.addressModeU;
		sampler.addressModeW = sampler.addressModeU;
		sampler.mipLodBias = 0.0f;
		sampler.maxAnisotropy = 1.0f;
		sampler.minLod = 0.0f;
		sampler.maxLod = 1.0f;
		sampler.borderColor = VK_BORDER_COLOR_FLOAT_OPAQUE_WHITE;
		VK_CHECK_RESULT(vkCreateSampler(device, &sampler, nullptr, &colorSampler));
	}

	void LoadAssets()
	{
		vks::ModelCreateInfo modelCreateInfo;
		modelCreateInfo.scale = glm::vec3(0.5f);
		modelCreateInfo.uvscale = glm::vec2(1.0f);
		modelCreateInfo.center = glm::vec3(0.0f, 0.0f, 0.0f);
		models.scene.LoadFromFile(GetAssetPath() + "models/sibenik/sibenik.dae", vertexLayout, &modelCreateInfo, vulkanDevice, queue);
	}

	void BuildCommandBuffers()
	{
		VkCommandBufferBeginInfo cmdBufInfo = vks::initializers::CommandBufferBeginInfo();

		VkDeviceSize offsets[1] = { 0 };

		for (int32_t i = 0; i < drawCmdBuffers.size(); ++i)
		{
			VK_CHECK_RESULT(vkBeginCommandBuffer(drawCmdBuffers[i], &cmdBufInfo));

			/*
				Offscreen SSAO generation
			*/
			{
				// Clear values for all attachments written in the fragment sahder
				std::vector<VkClearValue> clearValues(4);
				clearValues[0].color = { { 0.0f, 0.0f, 0.0f, 1.0f } };
				clearValues[1].color = { { 0.0f, 0.0f, 0.0f, 1.0f } };
				clearValues[2].color = { { 0.0f, 0.0f, 0.0f, 1.0f } };
				clearValues[3].depthStencil = { 1.0f, 0 };

				VkRenderPassBeginInfo renderPassBeginInfo = vks::initializers::RenderPassBeginInfo();
				renderPassBeginInfo.renderPass = frameBuffers.offscreen.renderPass;
				renderPassBeginInfo.framebuffer = frameBuffers.offscreen.frameBuffer;
				renderPassBeginInfo.renderArea.extent.width = frameBuffers.offscreen.width;
				renderPassBeginInfo.renderArea.extent.height = frameBuffers.offscreen.height;
				renderPassBeginInfo.clearValueCount = static_cast<uint32_t>(clearValues.size());
				renderPassBeginInfo.pClearValues = clearValues.data();

				/*
					First pass: Fill G-Buffer components (positions+depth, normals, albedo) using MRT
				*/

				vkCmdBeginRenderPass(drawCmdBuffers[i], &renderPassBeginInfo, VK_SUBPASS_CONTENTS_INLINE);

				VkViewport viewport = vks::initializers::Viewport((float)frameBuffers.offscreen.width, (float)frameBuffers.offscreen.height, 0.0f, 1.0f);
				vkCmdSetViewport(drawCmdBuffers[i], 0, 1, &viewport);

				VkRect2D scissor = vks::initializers::Rect2D(frameBuffers.offscreen.width, frameBuffers.offscreen.height, 0, 0);
				vkCmdSetScissor(drawCmdBuffers[i], 0, 1, &scissor);

				vkCmdBindPipeline(drawCmdBuffers[i], VK_PIPELINE_BIND_POINT_GRAPHICS, pipelines.offscreen);

				vkCmdBindDescriptorSets(drawCmdBuffers[i], VK_PIPELINE_BIND_POINT_GRAPHICS, pipelineLayouts.gBuffer, 0, 1, &descriptorSets.floor, 0, NULL);
				vkCmdBindVertexBuffers(drawCmdBuffers[i], 0, 1, &models.scene.vertices.buffer, offsets);
				vkCmdBindIndexBuffer(drawCmdBuffers[i], models.scene.indices.buffer, 0, VK_INDEX_TYPE_UINT32);
				vkCmdDrawIndexed(drawCmdBuffers[i], models.scene.indexCount, 1, 0, 0, 0);

				vkCmdEndRenderPass(drawCmdBuffers[i]);

				/*
					Second pass: SSAO generation
				*/

				clearValues[0].color = { { 0.0f, 0.0f, 0.0f, 1.0f } };
				clearValues[1].depthStencil = { 1.0f, 0 };

				renderPassBeginInfo.framebuffer = frameBuffers.ssao.frameBuffer;
				renderPassBeginInfo.renderPass = frameBuffers.ssao.renderPass;
				renderPassBeginInfo.renderArea.extent.width = frameBuffers.ssao.width;
				renderPassBeginInfo.renderArea.extent.height = frameBuffers.ssao.height;
				renderPassBeginInfo.clearValueCount = 2;
				renderPassBeginInfo.pClearValues = clearValues.data();

				vkCmdBeginRenderPass(drawCmdBuffers[i], &renderPassBeginInfo, VK_SUBPASS_CONTENTS_INLINE);

				viewport = vks::initializers::Viewport((float)frameBuffers.ssao.width, (float)frameBuffers.ssao.height, 0.0f, 1.0f);
				vkCmdSetViewport(drawCmdBuffers[i], 0, 1, &viewport);
				scissor = vks::initializers::Rect2D(frameBuffers.ssao.width, frameBuffers.ssao.height, 0, 0);
				vkCmdSetScissor(drawCmdBuffers[i], 0, 1, &scissor);

				vkCmdBindDescriptorSets(drawCmdBuffers[i], VK_PIPELINE_BIND_POINT_GRAPHICS, pipelineLayouts.ssao, 0, 1, &descriptorSets.ssao, 0, NULL);
				vkCmdBindPipeline(drawCmdBuffers[i], VK_PIPELINE_BIND_POINT_GRAPHICS, pipelines.ssao);
				vkCmdDraw(drawCmdBuffers[i], 3, 1, 0, 0);

				vkCmdEndRenderPass(drawCmdBuffers[i]);

				/*
					Third pass: SSAO blur
				*/

				renderPassBeginInfo.framebuffer = frameBuffers.ssaoBlur.frameBuffer;
				renderPassBeginInfo.renderPass = frameBuffers.ssaoBlur.renderPass;
				renderPassBeginInfo.renderArea.extent.width = frameBuffers.ssaoBlur.width;
				renderPassBeginInfo.renderArea.extent.height = frameBuffers.ssaoBlur.height;

				vkCmdBeginRenderPass(drawCmdBuffers[i], &renderPassBeginInfo, VK_SUBPASS_CONTENTS_INLINE);

				viewport = vks::initializers::Viewport((float)frameBuffers.ssaoBlur.width, (float)frameBuffers.ssaoBlur.height, 0.0f, 1.0f);
				vkCmdSetViewport(drawCmdBuffers[i], 0, 1, &viewport);
				scissor = vks::initializers::Rect2D(frameBuffers.ssaoBlur.width, frameBuffers.ssaoBlur.height, 0, 0);
				vkCmdSetScissor(drawCmdBuffers[i], 0, 1, &scissor);

				vkCmdBindDescriptorSets(drawCmdBuffers[i], VK_PIPELINE_BIND_POINT_GRAPHICS, pipelineLayouts.ssaoBlur, 0, 1, &descriptorSets.ssaoBlur, 0, NULL);
				vkCmdBindPipeline(drawCmdBuffers[i], VK_PIPELINE_BIND_POINT_GRAPHICS, pipelines.ssaoBlur);
				vkCmdDraw(drawCmdBuffers[i], 3, 1, 0, 0);

				vkCmdEndRenderPass(drawCmdBuffers[i]);
			}

			/*
				Note: Explicit synchronization is not required between the render pass, as this is done implicit via sub pass dependencies
			*/

			/*
				Final render pass: Scene rendering with applied radial blur
			*/
			{
				std::vector<VkClearValue> clearValues(2);
				clearValues[0].color = defaultClearColor;
				clearValues[1].depthStencil = { 1.0f, 0 };

				VkRenderPassBeginInfo renderPassBeginInfo = vks::initializers::RenderPassBeginInfo();
				renderPassBeginInfo.renderPass = renderPass;
				renderPassBeginInfo.framebuffer = __super::frameBuffers[i];
				renderPassBeginInfo.renderArea.extent.width = width;
				renderPassBeginInfo.renderArea.extent.height = height;
				renderPassBeginInfo.clearValueCount = 2;
				renderPassBeginInfo.pClearValues = clearValues.data();

				vkCmdBeginRenderPass(drawCmdBuffers[i], &renderPassBeginInfo, VK_SUBPASS_CONTENTS_INLINE);

				VkViewport viewport = vks::initializers::Viewport((float)width, (float)height, 0.0f, 1.0f);
				vkCmdSetViewport(drawCmdBuffers[i], 0, 1, &viewport);

				VkRect2D scissor = vks::initializers::Rect2D(width, height, 0, 0);
				vkCmdSetScissor(drawCmdBuffers[i], 0, 1, &scissor);

				vkCmdBindDescriptorSets(drawCmdBuffers[i], VK_PIPELINE_BIND_POINT_GRAPHICS, pipelineLayouts.composition, 0, 1, &descriptorSets.composition, 0, NULL);

				// Final composition pass
				vkCmdBindPipeline(drawCmdBuffers[i], VK_PIPELINE_BIND_POINT_GRAPHICS, pipelines.composition);
				vkCmdDraw(drawCmdBuffers[i], 3, 1, 0, 0);

				DrawUI(drawCmdBuffers[i]);

				vkCmdEndRenderPass(drawCmdBuffers[i]);
			}

			VK_CHECK_RESULT(vkEndCommandBuffer(drawCmdBuffers[i]));
		}
	}

	void SetupDescriptorPool()
	{
		std::vector<VkDescriptorPoolSize> poolSizes = {
			vks::initializers::DescriptorPoolSize(VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 10),
			vks::initializers::DescriptorPoolSize(VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 12)
		};
		VkDescriptorPoolCreateInfo descriptorPoolInfo = vks::initializers::DescriptorPoolCreateInfo(poolSizes, descriptorSets.count);
		VK_CHECK_RESULT(vkCreateDescriptorPool(device, &descriptorPoolInfo, nullptr, &descriptorPool));
	}

	void SetupLayoutsAndDescriptors()
	{
		std::vector<VkDescriptorSetLayoutBinding> setLayoutBindings;
		VkDescriptorSetLayoutCreateInfo setLayoutCreateInfo;
		VkPipelineLayoutCreateInfo pipelineLayoutCreateInfo = vks::initializers::PipelineLayoutCreateInfo();
		VkDescriptorSetAllocateInfo descriptorAllocInfo = vks::initializers::DescriptorSetAllocateInfo(descriptorPool, nullptr, 1);
		std::vector<VkWriteDescriptorSet> writeDescriptorSets;
		std::vector<VkDescriptorImageInfo> imageDescriptors;

		// G-Buffer creation (offscreen scene rendering)
		setLayoutBindings = {
			vks::initializers::DescriptorSetLayoutBinding(VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, VK_SHADER_STAGE_VERTEX_BIT, 0),								// VS UBO
			vks::initializers::DescriptorSetLayoutBinding(VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_FRAGMENT_BIT, 1),						// FS Color
		};
		setLayoutCreateInfo = vks::initializers::DescriptorSetLayoutCreateInfo(setLayoutBindings.data(), static_cast<uint32_t>(setLayoutBindings.size()));
		VK_CHECK_RESULT(vkCreateDescriptorSetLayout(device, &setLayoutCreateInfo, nullptr, &descriptorSetLayouts.gBuffer));
		pipelineLayoutCreateInfo.pSetLayouts = &descriptorSetLayouts.gBuffer;
		VK_CHECK_RESULT(vkCreatePipelineLayout(device, &pipelineLayoutCreateInfo, nullptr, &pipelineLayouts.gBuffer));
		descriptorAllocInfo.pSetLayouts = &descriptorSetLayouts.gBuffer;
		VK_CHECK_RESULT(vkAllocateDescriptorSets(device, &descriptorAllocInfo, &descriptorSets.floor));
		writeDescriptorSets = {
			vks::initializers::WriteDescriptorSet(descriptorSets.floor, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 0, &uniformBuffers.sceneMatrices.descriptor),
		};
		vkUpdateDescriptorSets(device, static_cast<uint32_t>(writeDescriptorSets.size()), writeDescriptorSets.data(), 0, NULL);

		// SSAO Generation
		setLayoutBindings = {
			vks::initializers::DescriptorSetLayoutBinding(VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_FRAGMENT_BIT, 0),						// FS Position+Depth
			vks::initializers::DescriptorSetLayoutBinding(VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_FRAGMENT_BIT, 1),						// FS Normals
			vks::initializers::DescriptorSetLayoutBinding(VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_FRAGMENT_BIT, 2),						// FS SSAO Noise
			vks::initializers::DescriptorSetLayoutBinding(VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, VK_SHADER_STAGE_FRAGMENT_BIT, 3),								// FS SSAO Kernel UBO
			vks::initializers::DescriptorSetLayoutBinding(VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, VK_SHADER_STAGE_FRAGMENT_BIT, 4),								// FS Params UBO
		};
		setLayoutCreateInfo = vks::initializers::DescriptorSetLayoutCreateInfo(setLayoutBindings.data(), static_cast<uint32_t>(setLayoutBindings.size()));
		VK_CHECK_RESULT(vkCreateDescriptorSetLayout(device, &setLayoutCreateInfo, nullptr, &descriptorSetLayouts.ssao));
		pipelineLayoutCreateInfo.pSetLayouts = &descriptorSetLayouts.ssao;
		VK_CHECK_RESULT(vkCreatePipelineLayout(device, &pipelineLayoutCreateInfo, nullptr, &pipelineLayouts.ssao));
		descriptorAllocInfo.pSetLayouts = &descriptorSetLayouts.ssao;
		VK_CHECK_RESULT(vkAllocateDescriptorSets(device, &descriptorAllocInfo, &descriptorSets.ssao));
		imageDescriptors = {
			vks::initializers::DescriptorImageInfo(colorSampler, frameBuffers.offscreen.position.view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL),
			vks::initializers::DescriptorImageInfo(colorSampler, frameBuffers.offscreen.normal.view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL),
		};
		writeDescriptorSets = {
			vks::initializers::WriteDescriptorSet(descriptorSets.ssao, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 0, &imageDescriptors[0]),					// FS Position+Depth
			vks::initializers::WriteDescriptorSet(descriptorSets.ssao, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, &imageDescriptors[1]),					// FS Normals
			vks::initializers::WriteDescriptorSet(descriptorSets.ssao, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 2, &textures.ssaoNoise.descriptor),		// FS SSAO Noise
			vks::initializers::WriteDescriptorSet(descriptorSets.ssao, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 3, &uniformBuffers.ssaoKernel.descriptor),		// FS SSAO Kernel UBO
			vks::initializers::WriteDescriptorSet(descriptorSets.ssao, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 4, &uniformBuffers.ssaoParams.descriptor),		// FS SSAO Params UBO
		};
		vkUpdateDescriptorSets(device, static_cast<uint32_t>(writeDescriptorSets.size()), writeDescriptorSets.data(), 0, NULL);

		// SSAO Blur
		setLayoutBindings = {
			vks::initializers::DescriptorSetLayoutBinding(VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_FRAGMENT_BIT, 0),						// FS Sampler SSAO
		};
		setLayoutCreateInfo = vks::initializers::DescriptorSetLayoutCreateInfo(setLayoutBindings.data(), static_cast<uint32_t>(setLayoutBindings.size()));
		VK_CHECK_RESULT(vkCreateDescriptorSetLayout(device, &setLayoutCreateInfo, nullptr, &descriptorSetLayouts.ssaoBlur));
		pipelineLayoutCreateInfo.pSetLayouts = &descriptorSetLayouts.ssaoBlur;
		VK_CHECK_RESULT(vkCreatePipelineLayout(device, &pipelineLayoutCreateInfo, nullptr, &pipelineLayouts.ssaoBlur));
		descriptorAllocInfo.pSetLayouts = &descriptorSetLayouts.ssaoBlur;
		VK_CHECK_RESULT(vkAllocateDescriptorSets(device, &descriptorAllocInfo, &descriptorSets.ssaoBlur));
		imageDescriptors = {
			vks::initializers::DescriptorImageInfo(colorSampler, frameBuffers.ssao.color.view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL),
		};
		writeDescriptorSets = {
			vks::initializers::WriteDescriptorSet(descriptorSets.ssaoBlur, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 0, &imageDescriptors[0]),
		};
		vkUpdateDescriptorSets(device, static_cast<uint32_t>(writeDescriptorSets.size()), writeDescriptorSets.data(), 0, NULL);

		// Composition
		setLayoutBindings = {
			vks::initializers::DescriptorSetLayoutBinding(VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_FRAGMENT_BIT, 0),						// FS Position+Depth
			vks::initializers::DescriptorSetLayoutBinding(VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_FRAGMENT_BIT, 1),						// FS Normals
			vks::initializers::DescriptorSetLayoutBinding(VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_FRAGMENT_BIT, 2),						// FS Albedo
			vks::initializers::DescriptorSetLayoutBinding(VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_FRAGMENT_BIT, 3),						// FS SSAO
			vks::initializers::DescriptorSetLayoutBinding(VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_FRAGMENT_BIT, 4),						// FS SSAO blurred
			vks::initializers::DescriptorSetLayoutBinding(VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, VK_SHADER_STAGE_FRAGMENT_BIT, 5),								// FS Lights UBO
		};
		setLayoutCreateInfo = vks::initializers::DescriptorSetLayoutCreateInfo(setLayoutBindings.data(), static_cast<uint32_t>(setLayoutBindings.size()));
		VK_CHECK_RESULT(vkCreateDescriptorSetLayout(device, &setLayoutCreateInfo, nullptr, &descriptorSetLayouts.composition));
		pipelineLayoutCreateInfo.pSetLayouts = &descriptorSetLayouts.composition;
		VK_CHECK_RESULT(vkCreatePipelineLayout(device, &pipelineLayoutCreateInfo, nullptr, &pipelineLayouts.composition));
		descriptorAllocInfo.pSetLayouts = &descriptorSetLayouts.composition;
		VK_CHECK_RESULT(vkAllocateDescriptorSets(device, &descriptorAllocInfo, &descriptorSets.composition));
		imageDescriptors = {
			vks::initializers::DescriptorImageInfo(colorSampler, frameBuffers.offscreen.position.view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL),
			vks::initializers::DescriptorImageInfo(colorSampler, frameBuffers.offscreen.normal.view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL),
			vks::initializers::DescriptorImageInfo(colorSampler, frameBuffers.offscreen.albedo.view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL),
			vks::initializers::DescriptorImageInfo(colorSampler, frameBuffers.ssao.color.view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL),
			vks::initializers::DescriptorImageInfo(colorSampler, frameBuffers.ssaoBlur.color.view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL),
		};
		writeDescriptorSets = {
			vks::initializers::WriteDescriptorSet(descriptorSets.composition, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 0, &imageDescriptors[0]),			// FS Sampler Position+Depth
			vks::initializers::WriteDescriptorSet(descriptorSets.composition, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, &imageDescriptors[1]),			// FS Sampler Normals
			vks::initializers::WriteDescriptorSet(descriptorSets.composition, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 2, &imageDescriptors[2]),			// FS Sampler Albedo
			vks::initializers::WriteDescriptorSet(descriptorSets.composition, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 3, &imageDescriptors[3]),			// FS Sampler SSAO
			vks::initializers::WriteDescriptorSet(descriptorSets.composition, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 4, &imageDescriptors[4]),			// FS Sampler SSAO blurred
			vks::initializers::WriteDescriptorSet(descriptorSets.composition, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 5, &uniformBuffers.ssaoParams.descriptor),	// FS SSAO Params UBO
		};
		vkUpdateDescriptorSets(device, static_cast<uint32_t>(writeDescriptorSets.size()), writeDescriptorSets.data(), 0, NULL);
	}

	void PreparePipelines()
	{
		VkPipelineInputAssemblyStateCreateInfo inputAssemblyState = vks::initializers::PipelineInputAssemblyStateCreateInfo(VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST, 0, VK_FALSE);
		VkPipelineRasterizationStateCreateInfo rasterizationState = vks::initializers::PipelineRasterizationStateCreateInfo(VK_POLYGON_MODE_FILL, VK_CULL_MODE_BACK_BIT, VK_FRONT_FACE_CLOCKWISE, 0);
		VkPipelineColorBlendAttachmentState blendAttachmentState = vks::initializers::PipelineColorBlendAttachmentState(0xf, VK_FALSE);
		VkPipelineColorBlendStateCreateInfo colorBlendState = vks::initializers::PipelineColorBlendStateCreateInfo(1, &blendAttachmentState);
		VkPipelineDepthStencilStateCreateInfo depthStencilState = vks::initializers::PipelineDepthStencilStateCreateInfo(VK_TRUE, VK_TRUE, VK_COMPARE_OP_LESS_OR_EQUAL);
		VkPipelineViewportStateCreateInfo viewportState = vks::initializers::PipelineViewportStateCreateInfo(1, 1, 0);
		VkPipelineMultisampleStateCreateInfo multisampleState = vks::initializers::PipelineMultisampleStateCreateInfo(VK_SAMPLE_COUNT_1_BIT, 0);
		std::vector<VkDynamicState> dynamicStateEnables = { VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR };
		VkPipelineDynamicStateCreateInfo dynamicState = vks::initializers::PipelineDynamicStateCreateInfo(dynamicStateEnables);
		std::array<VkPipelineShaderStageCreateInfo, 2> shaderStages;

		// Vertex input state for scene rendering
		const std::vector<VkVertexInputBindingDescription> vertexInputBindings = {
			vks::initializers::VertexInputBindingDescription(0, vertexLayout.Stride(), VK_VERTEX_INPUT_RATE_VERTEX),
		};
		const std::vector<VkVertexInputAttributeDescription> vertexInputAttributes = {
			vks::initializers::VertexInputAttributeDescription(0, 0, VK_FORMAT_R32G32B32_SFLOAT, 0),					// Location 0: Position
			vks::initializers::VertexInputAttributeDescription(0, 1, VK_FORMAT_R32G32_SFLOAT, sizeof(float) * 3),		// Location 1: UV
			vks::initializers::VertexInputAttributeDescription(0, 2, VK_FORMAT_R32G32B32_SFLOAT, sizeof(float) * 5),	// Location 2: Color
			vks::initializers::VertexInputAttributeDescription(0, 3, VK_FORMAT_R32G32B32_SFLOAT, sizeof(float) * 8),	// Location 3: Normal
		};
		VkPipelineVertexInputStateCreateInfo vertexInputState = vks::initializers::PipelineVertexInputStateCreateInfo();
		vertexInputState.vertexBindingDescriptionCount = static_cast<uint32_t>(vertexInputBindings.size());
		vertexInputState.pVertexBindingDescriptions = vertexInputBindings.data();
		vertexInputState.vertexAttributeDescriptionCount = static_cast<uint32_t>(vertexInputAttributes.size());
		vertexInputState.pVertexAttributeDescriptions = vertexInputAttributes.data();

		// Empty vertex input state for fullscreen passes
		VkPipelineVertexInputStateCreateInfo emptyVertexInputState = vks::initializers::PipelineVertexInputStateCreateInfo();

		VkGraphicsPipelineCreateInfo pipelineCreateInfo = vks::initializers::PipelineCreateInfo(pipelineLayouts.composition, renderPass, 0);
		pipelineCreateInfo.pVertexInputState = &emptyVertexInputState;
		pipelineCreateInfo.pInputAssemblyState = &inputAssemblyState;
		pipelineCreateInfo.pRasterizationState = &rasterizationState;
		pipelineCreateInfo.pColorBlendState = &colorBlendState;
		pipelineCreateInfo.pMultisampleState = &multisampleState;
		pipelineCreateInfo.pViewportState = &viewportState;
		pipelineCreateInfo.pDepthStencilState = &depthStencilState;
		pipelineCreateInfo.pDynamicState = &dynamicState;
		pipelineCreateInfo.stageCount = static_cast<uint32_t>(shaderStages.size());
		pipelineCreateInfo.pStages = shaderStages.data();

		shaderStages[0] = LoadShader(GetShadersPath() + "ssao/fullscreen.vert.spv", VK_SHADER_STAGE_VERTEX_BIT);
		shaderStages[1] = LoadShader(GetShadersPath() + "ssao/composition.frag.spv", VK_SHADER_STAGE_FRAGMENT_BIT);
		VK_CHECK_RESULT(vkCreateGraphicsPipelines(device, pipelineCache, 1, &pipelineCreateInfo, nullptr, &pipelines.composition));

		// SSAO Pass
		{
			shaderStages[1] = LoadShader(GetShadersPath() + "ssao/ssao.frag.spv", VK_SHADER_STAGE_FRAGMENT_BIT);
			// Set constant parameters via specialization constants
			std::array<VkSpecializationMapEntry, 2> specializationMapEntries;
			specializationMapEntries[0] = vks::initializers::SpecializationMapEntry(0, 0, sizeof(uint32_t));				// SSAO Kernel size
			specializationMapEntries[1] = vks::initializers::SpecializationMapEntry(1, sizeof(uint32_t), sizeof(float));	// SSAO radius
			struct {
				uint32_t kernelSize = SSAO_KERNEL_SIZE;
				float radius = SSAO_RADIUS;
			} specializationData;
			VkSpecializationInfo specializationInfo = vks::initializers::SpecializationInfo(2, specializationMapEntries.data(), sizeof(specializationData), &specializationData);
			shaderStages[1].pSpecializationInfo = &specializationInfo;
			pipelineCreateInfo.renderPass = frameBuffers.ssao.renderPass;
			pipelineCreateInfo.layout = pipelineLayouts.ssao;
			VK_CHECK_RESULT(vkCreateGraphicsPipelines(device, pipelineCache, 1, &pipelineCreateInfo, nullptr, &pipelines.ssao));
		}

		// SSAO blur pass
		{
			shaderStages[1] = LoadShader(GetShadersPath() + "ssao/blur.frag.spv", VK_SHADER_STAGE_FRAGMENT_BIT);
			pipelineCreateInfo.renderPass = frameBuffers.ssaoBlur.renderPass;
			pipelineCreateInfo.layout = pipelineLayouts.ssaoBlur;
			VK_CHECK_RESULT(vkCreateGraphicsPipelines(device, pipelineCache, 1, &pipelineCreateInfo, nullptr, &pipelines.ssaoBlur));
		}

		// Fill G-Buffer
		{
			shaderStages[0] = LoadShader(GetShadersPath() + "ssao/gbuffer.vert.spv", VK_SHADER_STAGE_VERTEX_BIT);
			shaderStages[1] = LoadShader(GetShadersPath() + "ssao/gbuffer.frag.spv", VK_SHADER_STAGE_FRAGMENT_BIT);
			pipelineCreateInfo.pVertexInputState = &vertexInputState;
			pipelineCreateInfo.renderPass = frameBuffers.offscreen.renderPass;
			pipelineCreateInfo.layout = pipelineLayouts.gBuffer;
			// Blend attachment states required for all color attachments
			// This is important, as color write mask will otherwise be 0x0 and you
			// won't see anything rendered to the attachment
			std::array<VkPipelineColorBlendAttachmentState, 3> blendAttachmentStates = {
				vks::initializers::PipelineColorBlendAttachmentState(0xf, VK_FALSE),
				vks::initializers::PipelineColorBlendAttachmentState(0xf, VK_FALSE),
				vks::initializers::PipelineColorBlendAttachmentState(0xf, VK_FALSE)
			};
			colorBlendState.attachmentCount = static_cast<uint32_t>(blendAttachmentStates.size());
			colorBlendState.pAttachments = blendAttachmentStates.data();
			VK_CHECK_RESULT(vkCreateGraphicsPipelines(device, pipelineCache, 1, &pipelineCreateInfo, nullptr, &pipelines.offscreen));
		}
	}

	float Lerp(float a, float b, float f)
	{
		return a + f * (b - a);
	}

	// Prepare and initialize uniform buffer containing shader uniforms
	void PrepareUniformBuffers()
	{
		// Scene matrices
		vulkanDevice->CreateBuffer(
			VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
			VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
			&uniformBuffers.sceneMatrices,
			sizeof(uboSceneMatrices));

		// SSAO parameters
		vulkanDevice->CreateBuffer(
			VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
			VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
			&uniformBuffers.ssaoParams,
			sizeof(uboSSAOParams));

		// Update
		UpdateUniformBufferMatrices();
		UpdateUniformBufferSSAOParams();

		// SSAO
		std::default_random_engine rndEngine(benchmark.active ? 0 : (unsigned)time(nullptr));
		std::uniform_real_distribution<float> rndDist(0.0f, 1.0f);

		// Sample kernel
		std::vector<glm::vec4> ssaoKernel(SSAO_KERNEL_SIZE);
		for (uint32_t i = 0; i < SSAO_KERNEL_SIZE; ++i)
		{
			glm::vec3 sample(rndDist(rndEngine) * 2.0 - 1.0, rndDist(rndEngine) * 2.0 - 1.0, rndDist(rndEngine));
			sample = glm::normalize(sample);
			sample *= rndDist(rndEngine);
			float scale = float(i) / float(SSAO_KERNEL_SIZE);
			scale = Lerp(0.1f, 1.0f, scale * scale);
			ssaoKernel[i] = glm::vec4(sample * scale, 0.0f);
		}

		// Upload as UBO
		vulkanDevice->CreateBuffer(
			VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
			VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
			&uniformBuffers.ssaoKernel,
			ssaoKernel.size() * sizeof(glm::vec4),
			ssaoKernel.data());

		// Random noise
		std::vector<glm::vec4> ssaoNoise(SSAO_NOISE_DIM * SSAO_NOISE_DIM);
		for (uint32_t i = 0; i < static_cast<uint32_t>(ssaoNoise.size()); i++)
		{
			ssaoNoise[i] = glm::vec4(rndDist(rndEngine) * 2.0f - 1.0f, rndDist(rndEngine) * 2.0f - 1.0f, 0.0f, 0.0f);
		}
		// Upload as texture
		textures.ssaoNoise.FromBuffer(ssaoNoise.data(), ssaoNoise.size() * sizeof(glm::vec4), VK_FORMAT_R32G32B32A32_SFLOAT, SSAO_NOISE_DIM, SSAO_NOISE_DIM, vulkanDevice, queue, VK_FILTER_NEAREST);
	}

	void UpdateUniformBufferMatrices()
	{
		uboSceneMatrices.projection = camera.matrices.perspective;
		uboSceneMatrices.view = camera.matrices.view;
		uboSceneMatrices.model = glm::mat4(1.0f);

		VK_CHECK_RESULT(uniformBuffers.sceneMatrices.Map());
		uniformBuffers.sceneMatrices.CopyTo(&uboSceneMatrices, sizeof(uboSceneMatrices));
		uniformBuffers.sceneMatrices.Unmap();
	}

	void UpdateUniformBufferSSAOParams()
	{
		uboSSAOParams.projection = camera.matrices.perspective;

		VK_CHECK_RESULT(uniformBuffers.ssaoParams.Map());
		uniformBuffers.ssaoParams.CopyTo(&uboSSAOParams, sizeof(uboSSAOParams));
		uniformBuffers.ssaoParams.Unmap();
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
		LoadAssets();
		PrepareOffscreenFramebuffers();
		PrepareUniformBuffers();
		SetupDescriptorPool();
		SetupLayoutsAndDescriptors();
		PreparePipelines();
		BuildCommandBuffers();
		prepared = true;
	}

	virtual void Render()
	{
		if (!prepared)
			return;
		Draw();
	}

	virtual void ViewChanged()
	{
		UpdateUniformBufferMatrices();
		UpdateUniformBufferSSAOParams();
	}

	virtual void OnUpdateUIOverlay(vks::UIOverlay* overlay)
	{
		if (overlay->Header("Settings")) {
			if (overlay->CheckBox("Enable SSAO", &uboSSAOParams.ssao)) {
				UpdateUniformBufferSSAOParams();
			}
			if (overlay->CheckBox("SSAO blur", &uboSSAOParams.ssaoBlur)) {
				UpdateUniformBufferSSAOParams();
			}
			if (overlay->CheckBox("SSAO pass only", &uboSSAOParams.ssaoOnly)) {
				UpdateUniformBufferSSAOParams();
			}
		}
	}
};

VULKAN_EXAMPLE_MAIN(VulkanExampleSSAO)