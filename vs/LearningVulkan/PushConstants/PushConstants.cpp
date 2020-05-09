#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <vector>

#define GLM_FORCE_RADIANS
#define GLM_FORCE_DEPTH_ZERO_TO_ONE
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

#include <vulkan/vulkan.h>
#include "VulkanBase.h"
#include "VulkanBuffer.hpp"
#include "VulkanModel.hpp"

#define VERTEX_BUFFER_BIND_ID 0
#define ENABLE_VALIDATION false

class VulkanExamplePushConstants : public VulkanBase
{
public:
	struct 
	{
		VkPipelineVertexInputStateCreateInfo inputState;
		std::vector<VkVertexInputBindingDescription> bindingDescriptions;
		std::vector<VkVertexInputAttributeDescription> attributeDescriptions;
	} vertices;

	// Vertex layout for the models
	vks::VertexLayout vertexLayout = vks::VertexLayout(
		{
			vks::VERTEX_COMPONENT_POSITION,
			vks::VERTEX_COMPONENT_NORMAL,
			vks::VERTEX_COMPONENT_UV,
			vks::VERTEX_COMPONENT_COLOR
		}
	);

	struct  
	{
		vks::Model scene;
	} models;

	vks::Buffer uniformBuffer;

	struct UBOVS
	{
		glm::mat4 projection;
		glm::mat4 modelView;
		glm::vec4 lightPos = glm::vec4(0.0, 0.0, -2.0, 1.0);
	} uboVS;

	struct  
	{
		VkPipeline solid;
	} pipelines;

	VkPipelineLayout pipelineLayout;
	VkDescriptorSet descriptorSet;
	VkDescriptorSetLayout descriptorSetLayout;

	// This array holds the light positions and will be updated via a push constant
	std::array<glm::vec4, 6> pushConstants;

	VulkanExamplePushConstants() : VulkanBase(ENABLE_VALIDATION)
	{
		title = "Push constants";
		camera.type = Camera::CameraType::lookat;
		camera.SetPosition(glm::vec3(0.0f, 0.0f, -30.0f));
		camera.SetRotation(glm::vec3(-32.5f, 45.0f, 0.0f));
		camera.SetRotationSpeed(0.5f);
		camera.SetPerspective(60.0f, (float)width / (float)height, 1.0f, 256.0f);
		timerSpeed *= 0.5f;
		settings.overlay = true;
	}

	~VulkanExamplePushConstants()
	{
		// Clean up used Vulkan resources 
		// Note : Inherited destructor cleans up resources stored in base class
		vkDestroyPipeline(device, pipelines.solid, nullptr);

		vkDestroyPipelineLayout(device, pipelineLayout, nullptr);
		vkDestroyDescriptorSetLayout(device, descriptorSetLayout, nullptr);

		models.scene.Destroy();

		uniformBuffer.Destroy();
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

			// Update light positions
			// w component = light radius scale
#define r 7.5f
#define sin_t sin(glm::radians(timer * 360))
#define cos_t cos(glm::radians(timer * 360))
#define y -4.0f
			pushConstants[0] = glm::vec4(r * 1.1 * sin_t, y, r * 1.1 * cos_t, 1.0f);
			pushConstants[1] = glm::vec4(-r * sin_t, y, -r * cos_t, 1.0f);
			pushConstants[2] = glm::vec4(r * 0.85f * sin_t, y, -sin_t * 2.5f, 1.5f);
			pushConstants[3] = glm::vec4(0.0f, y, r * 1.25f * cos_t, 1.5f);
			pushConstants[4] = glm::vec4(r * 2.25f * cos_t, y, 0.0f, 1.25f);
			pushConstants[5] = glm::vec4(r * 2.5f * cos_t, y, r * 2.5f * sin_t, 1.25f);
#undef r
#undef y
#undef sin_t
#undef cos_t

			// Submit via push constant (rather than a UBO)
			vkCmdPushConstants(drawCmdBuffers[i], pipelineLayout, VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(pushConstants), pushConstants.data());

			vkCmdBindPipeline(drawCmdBuffers[i], VK_PIPELINE_BIND_POINT_GRAPHICS, pipelines.solid);
			vkCmdBindDescriptorSets(drawCmdBuffers[i], VK_PIPELINE_BIND_POINT_GRAPHICS, pipelineLayout, 0, 1, &descriptorSet, 0, NULL);

			VkDeviceSize offsets[1] = { 0 };
			vkCmdBindVertexBuffers(drawCmdBuffers[i], VERTEX_BUFFER_BIND_ID, 1, &models.scene.vertices.buffer, offsets);
			vkCmdBindIndexBuffer(drawCmdBuffers[i], models.scene.indices.buffer, 0, VK_INDEX_TYPE_UINT32);

			vkCmdDrawIndexed(drawCmdBuffers[i], models.scene.indexCount, 1, 0, 0, 0);

			DrawUI(drawCmdBuffers[i]);

			vkCmdEndRenderPass(drawCmdBuffers[i]);

			VK_CHECK_RESULT(vkEndCommandBuffer(drawCmdBuffers[i]));
		}
	}

	void LoadAssets()
	{
		models.scene.LoadFromFile(GetAssetPath() + "models/samplescene.dae", vertexLayout, 0.35f, vulkanDevice, queue);
	}

	void SetupVertexDescriptions()
	{
		// Binding description
		vertices.bindingDescriptions.resize(1);
		vertices.bindingDescriptions[0] = vks::initializers::VertexInputBindingDescription(
			VERTEX_BUFFER_BIND_ID,
			vertexLayout.Stride(),
			VK_VERTEX_INPUT_RATE_VERTEX
		);

		// Attribute descriptions
		// Describes memory layout and shader positions
		vertices.attributeDescriptions.resize(4);
		// Location 0: Position
		vertices.attributeDescriptions[0] = vks::initializers::VertexInputAttributeDescription(
			VERTEX_BUFFER_BIND_ID,
			0,
			VK_FORMAT_R32G32B32_SFLOAT,
			0
		);
		// Location 1: Normal
		vertices.attributeDescriptions[1] = vks::initializers::VertexInputAttributeDescription(
			VERTEX_BUFFER_BIND_ID,
			1,
			VK_FORMAT_R32G32B32_SFLOAT,
			sizeof(float) * 3
		);
		// Location 2: Texture coordinates
		vertices.attributeDescriptions[2] = vks::initializers::VertexInputAttributeDescription(
			VERTEX_BUFFER_BIND_ID,
			2,
			VK_FORMAT_R32G32B32_SFLOAT,
			sizeof(float) * 6
		);
		// Location 3: Color
		vertices.attributeDescriptions[3] = vks::initializers::VertexInputAttributeDescription(
			VERTEX_BUFFER_BIND_ID,
			3,
			VK_FORMAT_R32G32B32_SFLOAT,
			sizeof(float) * 8
		);

		vertices.inputState = vks::initializers::PipelineVertexInputStateCreateInfo();
		vertices.inputState.vertexBindingDescriptionCount = vertices.bindingDescriptions.size();
		vertices.inputState.pVertexBindingDescriptions = vertices.bindingDescriptions.data();
		vertices.inputState.vertexAttributeDescriptionCount = vertices.attributeDescriptions.size();
		vertices.inputState.pVertexAttributeDescriptions = vertices.attributeDescriptions.data();
	}

	void SetupDescriptorPool()
	{
		// Exmaple uses one ubo
		std::vector<VkDescriptorPoolSize> poolSizes =
		{
			vks::initializers::DescriptorPoolSize(VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1)
		};

		VkDescriptorPoolCreateInfo descriptorPoolInfo = vks::initializers::DescriptorPoolCreateInfo(poolSizes.size(), poolSizes.data(), 2);

		VK_CHECK_RESULT(vkCreateDescriptorPool(device, &descriptorPoolInfo, nullptr, &descriptorPool));
	}

	void SetupDescriptorSetLayout()
	{
		std::vector<VkDescriptorSetLayoutBinding> setLayoutBindings =
		{
			// Binding 9 : Vextex shader uniform buffer
			vks::initializers::DescriptorSetLayoutBinding(VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, VK_SHADER_STAGE_VERTEX_BIT, 0)
		};

		VkDescriptorSetLayoutCreateInfo descriptorLayout = vks::initializers::DescriptorSetLayoutCreateInfo(setLayoutBindings.data(), setLayoutBindings.size());
		VK_CHECK_RESULT(vkCreateDescriptorSetLayout(device, &descriptorLayout, nullptr, &descriptorSetLayout));

		VkPipelineLayoutCreateInfo pipelineLayoutCreateInfo = vks::initializers::PipelineLayoutCreateInfo(&descriptorSetLayout, 1);

		// Define push constant
		// Example uses six light positions as push constants
		// 6 * 4 * 4 = 96 bytes
		// Spec requires a minimum of 128 bytes, bigger values
		// need to be checked against maxPushConstantsSize
		// But even at only 128 bytes, lots of stuff can fit 
		// inside push constants
		VkPushConstantRange pushConstantRange = vks::initializers::PushConstantRange(VK_SHADER_STAGE_VERTEX_BIT, sizeof(pushConstants), 0);

		// Push constant ranges are part of the pipeline layout
		pipelineLayoutCreateInfo.pushConstantRangeCount = 1;
		pipelineLayoutCreateInfo.pPushConstantRanges = &pushConstantRange;

		VK_CHECK_RESULT(vkCreatePipelineLayout(device, &pipelineLayoutCreateInfo, nullptr, &pipelineLayout));
	}

	void SetupDescriptorSet()
	{
		VkDescriptorSetAllocateInfo allocInfo = vks::initializers::DescriptorSetAllocateInfo(descriptorPool, &descriptorSetLayout, 1);
		VK_CHECK_RESULT(vkAllocateDescriptorSets(device, &allocInfo, &descriptorSet));

		// Binding 0 : Vertex shader uniform buffer
		VkWriteDescriptorSet writeDescriptorSet = vks::initializers::WriteDescriptorSet(descriptorSet, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 0, &uniformBuffer.descriptor);

		vkUpdateDescriptorSets(device, 1, &writeDescriptorSet, 0, NULL);
	}

	void PreparePipelines()
	{
		VkPipelineInputAssemblyStateCreateInfo inputAssemblyState = vks::initializers::PipelineInputAssemblyStateCreateInfo(VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST, 0, VK_FALSE);
		VkPipelineRasterizationStateCreateInfo rasterizationState = vks::initializers::PipelineRasterizationStateCreateInfo(VK_POLYGON_MODE_FILL, VK_CULL_MODE_BACK_BIT, VK_FRONT_FACE_CLOCKWISE, 0);
		VkPipelineColorBlendAttachmentState blendAttachmentState  = vks::initializers::PipelineColorBlendAttachmentState(0xf, VK_FALSE);
		VkPipelineColorBlendStateCreateInfo colorBlendState		  = vks::initializers::PipelineColorBlendStateCreateInfo(1, &blendAttachmentState);
		VkPipelineDepthStencilStateCreateInfo depthStencilState   = vks::initializers::PipelineDepthStencilStateCreateInfo(VK_TRUE, VK_TRUE, VK_COMPARE_OP_LESS_OR_EQUAL);
		VkPipelineViewportStateCreateInfo viewportState			  = vks::initializers::PipelineViewportStateCreateInfo(1, 1, 0);
		VkPipelineMultisampleStateCreateInfo multisampleState	  = vks::initializers::PipelineMultisampleStateCreateInfo(VK_SAMPLE_COUNT_1_BIT, 0);

		std::vector<VkDynamicState> dynamicStateEnables =
		{
			VK_DYNAMIC_STATE_VIEWPORT,
			VK_DYNAMIC_STATE_SCISSOR
		};
		VkPipelineDynamicStateCreateInfo dynamicState = vks::initializers::PipelineDynamicStateCreateInfo(dynamicStateEnables.data(), dynamicStateEnables.size(), 0);

		// Solid rendering pipeline
		// Load shaders
		std::array<VkPipelineShaderStageCreateInfo, 2> shaderStages;
		shaderStages[0] = LoadShader(GetAssetPath() + "shaders/pushconstants/lights.vert.spv", VK_SHADER_STAGE_VERTEX_BIT);
		shaderStages[1] = LoadShader(GetAssetPath() + "shaders/pushconstants/lights.frag.spv", VK_SHADER_STAGE_FRAGMENT_BIT);

		VkGraphicsPipelineCreateInfo pipelineCreateInfo = vks::initializers::PipelineCreateInfo(pipelineLayout, renderPass, 0);
		pipelineCreateInfo.pVertexInputState = &vertices.inputState;
		pipelineCreateInfo.pInputAssemblyState = &inputAssemblyState;
		pipelineCreateInfo.pRasterizationState = &rasterizationState;
		pipelineCreateInfo.pColorBlendState = &colorBlendState;
		pipelineCreateInfo.pMultisampleState = &multisampleState;
		pipelineCreateInfo.pViewportState = &viewportState;
		pipelineCreateInfo.pDepthStencilState = &depthStencilState;
		pipelineCreateInfo.pDynamicState = &dynamicState;
		pipelineCreateInfo.stageCount = shaderStages.size();
		pipelineCreateInfo.pStages = shaderStages.data();

		VK_CHECK_RESULT(vkCreateGraphicsPipelines(device, pipelineCache, 1, &pipelineCreateInfo, nullptr, &pipelines.solid));
	}

	void PrepareUniformBuffers()
	{
		// Vertex shader uniform buffer block
		VK_CHECK_RESULT(vulkanDevice->CreateBuffer(
			VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
			VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
			&uniformBuffer,
			sizeof(uboVS))
		);

		// Map persistent
		VK_CHECK_RESULT(uniformBuffer.Map());
		UpdateUniformBuffers();
	}

	void UpdateUniformBuffers()
	{
		uboVS.projection = camera.matrices.perspective;
		uboVS.modelView = camera.matrices.view;
		memcpy(uniformBuffer.mapped, &uboVS, sizeof(uboVS));
	}

	void Draw()
	{
		__super::PrepareFrame();

		// Command buffer to be submitted to the queue
		submitInfo.commandBufferCount = 1;
		submitInfo.pCommandBuffers = &drawCmdBuffers[currentBuffer];

		// Submit to queue
		VK_CHECK_RESULT(vkQueueSubmit(queue, 1, &submitInfo, VK_NULL_HANDLE));

		__super::SubmitFrame();
	}

	void Prepare()
	{
		__super::Prepare();

		// Check requested push constant size against hardware limit
		// Specs require 128 bytes, so if the device complies our push constant buffer should always fit into memory
		assert(sizeof(pushConstants) <= vulkanDevice->properties.limits.maxPushConstantsSize);

		LoadAssets();
		SetupVertexDescriptions();
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
		if (!paused)
			BuildCommandBuffers();
	}

	virtual void ViewChanged()
	{
		UpdateUniformBuffers();
	}
};

VULKAN_EXAMPLE_MAIN(VulkanExamplePushConstants)