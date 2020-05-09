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
#include "VulkanTexture.hpp"
#include "VulkanModel.hpp"
#include "VulkanBuffer.hpp"

#define VERTEX_BUFFER_BIND_ID 0
#define ENABLE_VALIDATION false

class VulkanExampleSpecializationConstants : public VulkanBase
{
public:
	struct 
	{
		VkPipelineVertexInputStateCreateInfo inputState;
		std::vector<VkVertexInputBindingDescription> bindingDescriptions;
		std::vector<VkVertexInputAttributeDescription> attributeDescriptions;
	} vertices;

	// Vertex layout for the models
	vks::VertexLayout vertexLayout = vks::VertexLayout({
		vks::VERTEX_COMPONENT_POSITION,
		vks::VERTEX_COMPONENT_NORMAL,
		vks::VERTEX_COMPONENT_UV,
		vks::VERTEX_COMPONENT_COLOR,
		});

	struct 
	{
		vks::Model cube;
	} models;

	struct 
	{
		vks::Texture2D colormap;
	} textures;

	vks::Buffer uniformBuffer;

	// Same uniform buffer layout as shader
	struct UBOVS 
	{
		glm::mat4 projection;
		glm::mat4 modelView;
		glm::vec4 lightPos = glm::vec4(0.0f, -2.0f, 1.0f, 0.0f);
	} uboVS;

	VkPipelineLayout pipelineLayout;
	VkDescriptorSet descriptorSet;
	VkDescriptorSetLayout descriptorSetLayout;

	struct 
	{
		VkPipeline phong;
		VkPipeline toon;
		VkPipeline textured;
	} pipelines;

	VulkanExampleSpecializationConstants() : VulkanBase(ENABLE_VALIDATION)
	{
		title = "Specialization constants";
		camera.type = Camera::CameraType::lookat;
		camera.SetPerspective(60.0f, ((float)width / 3.0f) / (float)height, 0.1f, 512.0f);
		camera.SetRotation(glm::vec3(-40.0f, -90.0f, 0.0f));
		camera.SetTranslation(glm::vec3(0.0f, 0.0f, -2.0f));
		settings.overlay = true;
	}

	~VulkanExampleSpecializationConstants()
	{
		vkDestroyPipeline(device, pipelines.phong, nullptr);
		vkDestroyPipeline(device, pipelines.textured, nullptr);
		vkDestroyPipeline(device, pipelines.toon, nullptr);

		vkDestroyPipelineLayout(device, pipelineLayout, nullptr);
		vkDestroyDescriptorSetLayout(device, descriptorSetLayout, nullptr);

		models.cube.Destroy();
		textures.colormap.Destroy();
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

			vkCmdBindDescriptorSets(drawCmdBuffers[i], VK_PIPELINE_BIND_POINT_GRAPHICS, pipelineLayout, 0, 1, &descriptorSet, 0, NULL);

			VkDeviceSize offsets[1] = { 0 };
			vkCmdBindVertexBuffers(drawCmdBuffers[i], VERTEX_BUFFER_BIND_ID, 1, &models.cube.vertices.buffer, offsets);
			vkCmdBindIndexBuffer(drawCmdBuffers[i], models.cube.indices.buffer, 0, VK_INDEX_TYPE_UINT32);

			// Left
			viewport.width = (float)width / 3.0f;
			vkCmdSetViewport(drawCmdBuffers[i], 0, 1, &viewport);
			vkCmdBindPipeline(drawCmdBuffers[i], VK_PIPELINE_BIND_POINT_GRAPHICS, pipelines.phong);

			vkCmdDrawIndexed(drawCmdBuffers[i], models.cube.indexCount, 1, 0, 0, 0);

			// Center
			viewport.x = (float)width / 3.0f;
			vkCmdSetViewport(drawCmdBuffers[i], 0, 1, &viewport);
			vkCmdBindPipeline(drawCmdBuffers[i], VK_PIPELINE_BIND_POINT_GRAPHICS, pipelines.toon);
			vkCmdDrawIndexed(drawCmdBuffers[i], models.cube.indexCount, 1, 0, 0, 0);

			// Right
			viewport.x = (float)width / 3.0f + (float)width / 3.0f;
			vkCmdSetViewport(drawCmdBuffers[i], 0, 1, &viewport);
			vkCmdBindPipeline(drawCmdBuffers[i], VK_PIPELINE_BIND_POINT_GRAPHICS, pipelines.textured);
			vkCmdDrawIndexed(drawCmdBuffers[i], models.cube.indexCount, 1, 0, 0, 0);

			DrawUI(drawCmdBuffers[i]);

			vkCmdEndRenderPass(drawCmdBuffers[i]);

			VK_CHECK_RESULT(vkEndCommandBuffer(drawCmdBuffers[i]));
		}
	}

	void LoadAssets()
	{
		models.cube.LoadFromFile(GetAssetPath() + "models/color_teapot_spheres.dae", vertexLayout, 0.1f, vulkanDevice, queue);
		textures.colormap.LoadFromFile(GetAssetPath() + "textures/metalplate_nomips_rgba.ktx", VK_FORMAT_R8G8B8A8_UNORM, vulkanDevice, queue);
	}

	void SetupVertexDescriptions()
	{
		// Binding description
		vertices.bindingDescriptions.resize(1);
		vertices.bindingDescriptions = {
			vks::initializers::VertexInputBindingDescription(VERTEX_BUFFER_BIND_ID, vertexLayout.Stride(), VK_VERTEX_INPUT_RATE_VERTEX),
		};

		// Attribute descriptions
		vertices.attributeDescriptions = {
			// Location 0 : Position
			vks::initializers::VertexInputAttributeDescription(
				VERTEX_BUFFER_BIND_ID,
				0,
				VK_FORMAT_R32G32B32_SFLOAT,
				0),
			// Location 1 : Color
			vks::initializers::VertexInputAttributeDescription(
				VERTEX_BUFFER_BIND_ID,
				1,
				VK_FORMAT_R32G32B32_SFLOAT,
				sizeof(float) * 3),
			// Location 3 : Texture coordinates
			vks::initializers::VertexInputAttributeDescription(
				VERTEX_BUFFER_BIND_ID,
				2,
				VK_FORMAT_R32G32_SFLOAT,
				sizeof(float) * 6),
			// Location 2 : Normal
			vks::initializers::VertexInputAttributeDescription(
				VERTEX_BUFFER_BIND_ID,
				3,
				VK_FORMAT_R32G32B32_SFLOAT,
				sizeof(float) * 8),
		};

		vertices.inputState = vks::initializers::PipelineVertexInputStateCreateInfo();
		vertices.inputState.vertexBindingDescriptionCount = static_cast<uint32_t>(vertices.bindingDescriptions.size());
		vertices.inputState.pVertexBindingDescriptions = vertices.bindingDescriptions.data();
		vertices.inputState.vertexAttributeDescriptionCount = static_cast<uint32_t>(vertices.attributeDescriptions.size());
		vertices.inputState.pVertexAttributeDescriptions = vertices.attributeDescriptions.data();
	}

	void SetupDescriptorPool()
	{
		std::vector<VkDescriptorPoolSize> poolSizes =
		{
			vks::initializers::DescriptorPoolSize(VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1),
			vks::initializers::DescriptorPoolSize(VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1)
		};

		VkDescriptorPoolCreateInfo descriptorPoolInfo =
			vks::initializers::DescriptorPoolCreateInfo(
				static_cast<uint32_t>(poolSizes.size()),
				poolSizes.data(),
				1);

		VK_CHECK_RESULT(vkCreateDescriptorPool(device, &descriptorPoolInfo, nullptr, &descriptorPool));
	}

	void SetupDescriptorSetLayout()
	{
		std::vector<VkDescriptorSetLayoutBinding> setLayoutBindings = {
			vks::initializers::DescriptorSetLayoutBinding(VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, VK_SHADER_STAGE_VERTEX_BIT, 0),
			vks::initializers::DescriptorSetLayoutBinding(VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_FRAGMENT_BIT, 1),
		};

		VkDescriptorSetLayoutCreateInfo descriptorLayout =
			vks::initializers::DescriptorSetLayoutCreateInfo(
				setLayoutBindings.data(),
				static_cast<uint32_t>(setLayoutBindings.size()));

		VK_CHECK_RESULT(vkCreateDescriptorSetLayout(device, &descriptorLayout, nullptr, &descriptorSetLayout));

		VkPipelineLayoutCreateInfo pPipelineLayoutCreateInfo =
			vks::initializers::PipelineLayoutCreateInfo(
				&descriptorSetLayout,
				1);

		VK_CHECK_RESULT(vkCreatePipelineLayout(device, &pPipelineLayoutCreateInfo, nullptr, &pipelineLayout));
	}

	void SetupDescriptorSet()
	{
		VkDescriptorSetAllocateInfo allocInfo =
			vks::initializers::DescriptorSetAllocateInfo(
				descriptorPool,
				&descriptorSetLayout,
				1);

		VK_CHECK_RESULT(vkAllocateDescriptorSets(device, &allocInfo, &descriptorSet));

		std::vector<VkWriteDescriptorSet> writeDescriptorSets = {
			vks::initializers::WriteDescriptorSet(descriptorSet, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 0, &uniformBuffer.descriptor),
			vks::initializers::WriteDescriptorSet(descriptorSet, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, &textures.colormap.descriptor),
		};

		vkUpdateDescriptorSets(device, static_cast<uint32_t>(writeDescriptorSets.size()), writeDescriptorSets.data(), 0, NULL);
	}

	void PreparePipelines()
	{
		VkPipelineInputAssemblyStateCreateInfo inputAssemblyState =
			vks::initializers::PipelineInputAssemblyStateCreateInfo(
				VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST,
				0,
				VK_FALSE);

		VkPipelineRasterizationStateCreateInfo rasterizationState =
			vks::initializers::PipelineRasterizationStateCreateInfo(
				VK_POLYGON_MODE_FILL,
				VK_CULL_MODE_NONE,
				VK_FRONT_FACE_CLOCKWISE,
				0);

		VkPipelineColorBlendAttachmentState blendAttachmentState =
			vks::initializers::PipelineColorBlendAttachmentState(
				0xf,
				VK_FALSE);

		VkPipelineColorBlendStateCreateInfo colorBlendState =
			vks::initializers::PipelineColorBlendStateCreateInfo(
				1,
				&blendAttachmentState);

		VkPipelineDepthStencilStateCreateInfo depthStencilState =
			vks::initializers::PipelineDepthStencilStateCreateInfo(
				VK_TRUE,
				VK_TRUE,
				VK_COMPARE_OP_LESS_OR_EQUAL);

		VkPipelineViewportStateCreateInfo viewportState =
			vks::initializers::PipelineViewportStateCreateInfo(1, 1, 0);

		VkPipelineMultisampleStateCreateInfo multisampleState =
			vks::initializers::PipelineMultisampleStateCreateInfo(
				VK_SAMPLE_COUNT_1_BIT,
				0);

		std::vector<VkDynamicState> dynamicStateEnables = {
			VK_DYNAMIC_STATE_VIEWPORT,
			VK_DYNAMIC_STATE_SCISSOR,
			VK_DYNAMIC_STATE_LINE_WIDTH,
		};
		VkPipelineDynamicStateCreateInfo dynamicState =
			vks::initializers::PipelineDynamicStateCreateInfo(
				dynamicStateEnables.data(),
				static_cast<uint32_t>(dynamicStateEnables.size()),
				0);

		std::array<VkPipelineShaderStageCreateInfo, 2> shaderStages;

		VkGraphicsPipelineCreateInfo pipelineCreateInfo =
			vks::initializers::PipelineCreateInfo(
				pipelineLayout,
				renderPass,
				0);

		pipelineCreateInfo.pVertexInputState = &vertices.inputState;
		pipelineCreateInfo.pInputAssemblyState = &inputAssemblyState;
		pipelineCreateInfo.pRasterizationState = &rasterizationState;
		pipelineCreateInfo.pColorBlendState = &colorBlendState;
		pipelineCreateInfo.pMultisampleState = &multisampleState;
		pipelineCreateInfo.pViewportState = &viewportState;
		pipelineCreateInfo.pDepthStencilState = &depthStencilState;
		pipelineCreateInfo.pDynamicState = &dynamicState;
		pipelineCreateInfo.stageCount = static_cast<uint32_t>(shaderStages.size());
		pipelineCreateInfo.pStages = shaderStages.data();

		// Prepare specialization data

		// Host data to take specialization constants from
		struct SpecializationData {
			// Sets the lighting model used in the fragment "uber" shader
			uint32_t lightingModel;
			// Parameter for the toon shading part of the fragment shader
			float toonDesaturationFactor = 0.5f;
		} specializationData;

		// Each shader constant of a shader stage corresponds to one map entry
		std::array<VkSpecializationMapEntry, 2> specializationMapEntries;
		// Shader bindings based on specialization constants are marked by the new "constant_id" layout qualifier:
		//	layout (constant_id = 0) const int LIGHTING_MODEL = 0;
		//	layout (constant_id = 1) const float PARAM_TOON_DESATURATION = 0.0f;

		// Map entry for the lighting model to be used by the fragment shader
		specializationMapEntries[0].constantID = 0;
		specializationMapEntries[0].size = sizeof(specializationData.lightingModel);
		specializationMapEntries[0].offset = 0;

		// Map entry for the toon shader parameter
		specializationMapEntries[1].constantID = 1;
		specializationMapEntries[1].size = sizeof(specializationData.toonDesaturationFactor);
		specializationMapEntries[1].offset = offsetof(SpecializationData, toonDesaturationFactor);

		// Prepare specialization info block for the shader stage
		VkSpecializationInfo specializationInfo{};
		specializationInfo.dataSize = sizeof(specializationData);
		specializationInfo.mapEntryCount = static_cast<uint32_t>(specializationMapEntries.size());
		specializationInfo.pMapEntries = specializationMapEntries.data();
		specializationInfo.pData = &specializationData;

		// Create pipelines
		// All pipelines will use the same "uber" shader and specialization constants to change branching and parameters of that shader
		shaderStages[0] = LoadShader(GetAssetPath() + "shaders/specializationconstants/uber.vert.spv", VK_SHADER_STAGE_VERTEX_BIT);
		shaderStages[1] = LoadShader(GetAssetPath() + "shaders/specializationconstants/uber.frag.spv", VK_SHADER_STAGE_FRAGMENT_BIT);
		// Specialization info is assigned is part of the shader stage (modul) and must be set after creating the module and before creating the pipeline
		shaderStages[1].pSpecializationInfo = &specializationInfo;

		// Solid phong shading
		specializationData.lightingModel = 0;
		VK_CHECK_RESULT(vkCreateGraphicsPipelines(device, pipelineCache, 1, &pipelineCreateInfo, nullptr, &pipelines.phong));

		// Phong and textured
		specializationData.lightingModel = 1;
		VK_CHECK_RESULT(vkCreateGraphicsPipelines(device, pipelineCache, 1, &pipelineCreateInfo, nullptr, &pipelines.toon));

		// Textured discard
		specializationData.lightingModel = 2;
		VK_CHECK_RESULT(vkCreateGraphicsPipelines(device, pipelineCache, 1, &pipelineCreateInfo, nullptr, &pipelines.textured));
	}

	// Prepare and initialize uniform buffer containing shader uniforms
	void PrepareUniformBuffers()
	{
		// Create the vertex shader uniform buffer block
		VK_CHECK_RESULT(vulkanDevice->CreateBuffer(
			VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
			VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
			&uniformBuffer,
			sizeof(uboVS)));

		// Map persistent
		VK_CHECK_RESULT(uniformBuffer.Map());

		UpdateUniformBuffers();
	}

	void UpdateUniformBuffers()
	{
		camera.SetPerspective(60.0f, ((float)width / 3.0f) / (float)height, 0.1f, 512.0f);

		uboVS.projection = camera.matrices.perspective;
		uboVS.modelView = camera.matrices.view;

		memcpy(uniformBuffer.mapped, &uboVS, sizeof(uboVS));
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
		if (!prepared) {
			return;
		}
		Draw();
		if (camera.updated) {
			UpdateUniformBuffers();
		}
	}

	virtual void WindowResized()
	{
		UpdateUniformBuffers();
	}
};

VULKAN_EXAMPLE_MAIN(VulkanExampleSpecializationConstants)