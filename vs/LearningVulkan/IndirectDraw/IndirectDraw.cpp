#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <time.h>
#include <vector>
#include <random>
#include <corecrt_math_defines.h>

#define GLM_FORCE_RADIANS
#define GLM_FORCE_DEPTH_ZERO_TO_ONE
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

#include <vulkan/vulkan.h>
#include "VulkanBase.h"
#include "VulkanBuffer.hpp"
#include "VulkanTexture.hpp"
#include "VulkanModel.hpp"

#define VERTEX_BUFFER_BIND_ID 0
#define INSTANCE_BUFFER_BIND_ID 1
#define ENABLE_VALIDATION false

// Number of instances per object
#if defined(__ANDROID__)
#define OBJECT_INSTANCE_COUNT 1024
// Circular range of plant distribution
#define PLANT_RADIUS 20.0f
#else
#define OBJECT_INSTANCE_COUNT 2048
// Circular range of plant distribution
#define PLANT_RADIUS 25.0f
#endif

class VulkanExampleIndirectDraw : public VulkanBase
{
public:
	struct {
		vks::Texture2DArray plants;
		vks::Texture2D ground;
	} textures;

	// Vertex layout for the models
	vks::VertexLayout vertexLayout = vks::VertexLayout({
		vks::VERTEX_COMPONENT_POSITION,
		vks::VERTEX_COMPONENT_NORMAL,
		vks::VERTEX_COMPONENT_UV,
		vks::VERTEX_COMPONENT_COLOR,
		});

	struct {
		vks::Model plants;
		vks::Model ground;
		vks::Model skysphere;
	} models;

	struct {
		VkPipelineVertexInputStateCreateInfo inputState;
		std::vector<VkVertexInputBindingDescription> bindingDescriptions;
		std::vector<VkVertexInputAttributeDescription> attributeDescriptions;
	} vertices;

	// Per-instance data block
	struct InstanceData {
		glm::vec3 pos;
		glm::vec3 rot;
		float scale;
		uint32_t texIndex;
	};

	// Contains the instanced data
	vks::Buffer instanceBuffer;
	// Contains the indirect drawing commands
	vks::Buffer indirectCommandsBuffer;
	uint32_t indirectDrawCount;

	struct {
		glm::mat4 projection;
		glm::mat4 view;
	} uboVS;

	struct {
		vks::Buffer scene;
	} uniformData;

	struct {
		VkPipeline plants;
		VkPipeline ground;
		VkPipeline skysphere;
	} pipelines;

	VkPipelineLayout pipelineLayout;
	VkDescriptorSet descriptorSet;
	VkDescriptorSetLayout descriptorSetLayout;

	VkSampler samplerRepeat;

	uint32_t objectCount = 0;

	// Store the indirect draw commands containing index offsets and instance count per object
	std::vector<VkDrawIndexedIndirectCommand> indirectCommands;

	VulkanExampleIndirectDraw() : VulkanBase(ENABLE_VALIDATION)
	{
		title = "Indirect rendering";
		camera.type = Camera::CameraType::firstperson;
		camera.SetPerspective(60.0f, (float)width / (float)height, 0.1f, 512.0f);
		camera.SetRotation(glm::vec3(-12.0f, 159.0f, 0.0f));
		camera.SetTranslation(glm::vec3(0.4f, 1.25f, 0.0f));
		camera.movementSpeed = 5.0f;
		settings.overlay = true;
	}

	~VulkanExampleIndirectDraw()
	{
		vkDestroyPipeline(device, pipelines.plants, nullptr);
		vkDestroyPipeline(device, pipelines.ground, nullptr);
		vkDestroyPipeline(device, pipelines.skysphere, nullptr);
		vkDestroyPipelineLayout(device, pipelineLayout, nullptr);
		vkDestroyDescriptorSetLayout(device, descriptorSetLayout, nullptr);
		models.plants.Destroy();
		models.ground.Destroy();
		models.skysphere.Destroy();
		textures.plants.Destroy();
		textures.ground.Destroy();
		instanceBuffer.Destroy();
		indirectCommandsBuffer.Destroy();
		uniformData.scene.Destroy();
	}

	// Enable physical device features required for this example
	virtual void GetEnabledFeatures()
	{
		// Example uses multi draw indirect if available
		if (deviceFeatures.multiDrawIndirect) {
			enabledFeatures.multiDrawIndirect = VK_TRUE;
		}
		// Enable anisotropic filtering if supported
		if (deviceFeatures.samplerAnisotropy) {
			enabledFeatures.samplerAnisotropy = VK_TRUE;
		}
		// Enable texture compression
		if (deviceFeatures.textureCompressionBC) {
			enabledFeatures.textureCompressionBC = VK_TRUE;
		}
		else if (deviceFeatures.textureCompressionASTC_LDR) {
			enabledFeatures.textureCompressionASTC_LDR = VK_TRUE;
		}
		else if (deviceFeatures.textureCompressionETC2) {
			enabledFeatures.textureCompressionETC2 = VK_TRUE;
		}
	};

	void BuildCommandBuffers()
	{
		VkCommandBufferBeginInfo cmdBufInfo = vks::initializers::CommandBufferBeginInfo();

		VkClearValue clearValues[2];
		clearValues[0].color = { { 0.18f, 0.27f, 0.5f, 0.0f } };
		clearValues[1].depthStencil = { 1.0f, 0 };

		VkRenderPassBeginInfo renderPassBeginInfo = vks::initializers::RenderPassBeginInfo();
		renderPassBeginInfo.renderPass = renderPass;
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

			VkDeviceSize offsets[1] = { 0 };
			vkCmdBindDescriptorSets(drawCmdBuffers[i], VK_PIPELINE_BIND_POINT_GRAPHICS, pipelineLayout, 0, 1, &descriptorSet, 0, NULL);

			// Plants
			vkCmdBindPipeline(drawCmdBuffers[i], VK_PIPELINE_BIND_POINT_GRAPHICS, pipelines.plants);
			// Binding point 0 : Mesh vertex buffer
			vkCmdBindVertexBuffers(drawCmdBuffers[i], VERTEX_BUFFER_BIND_ID, 1, &models.plants.vertices.buffer, offsets);
			// Binding point 1 : Instance data buffer
			vkCmdBindVertexBuffers(drawCmdBuffers[i], INSTANCE_BUFFER_BIND_ID, 1, &instanceBuffer.buffer, offsets);

			vkCmdBindIndexBuffer(drawCmdBuffers[i], models.plants.indices.buffer, 0, VK_INDEX_TYPE_UINT32);

			// If the multi draw feature is supported:
			// One draw call for an arbitrary number of ojects
			// Index offsets and instance count are taken from the indirect buffer
			if (vulkanDevice->features.multiDrawIndirect)
			{
				vkCmdDrawIndexedIndirect(drawCmdBuffers[i], indirectCommandsBuffer.buffer, 0, indirectDrawCount, sizeof(VkDrawIndexedIndirectCommand));
			}
			else
			{
				// If multi draw is not available, we must issue separate draw commands
				for (auto j = 0; j < indirectCommands.size(); j++)
				{
					vkCmdDrawIndexedIndirect(drawCmdBuffers[i], indirectCommandsBuffer.buffer, j * sizeof(VkDrawIndexedIndirectCommand), 1, sizeof(VkDrawIndexedIndirectCommand));
				}
			}

			// Ground
			vkCmdBindPipeline(drawCmdBuffers[i], VK_PIPELINE_BIND_POINT_GRAPHICS, pipelines.ground);
			vkCmdBindVertexBuffers(drawCmdBuffers[i], VERTEX_BUFFER_BIND_ID, 1, &models.ground.vertices.buffer, offsets);
			vkCmdBindIndexBuffer(drawCmdBuffers[i], models.ground.indices.buffer, 0, VK_INDEX_TYPE_UINT32);
			vkCmdDrawIndexed(drawCmdBuffers[i], models.ground.indexCount, 1, 0, 0, 0);
			// Skysphere
			vkCmdBindPipeline(drawCmdBuffers[i], VK_PIPELINE_BIND_POINT_GRAPHICS, pipelines.skysphere);
			vkCmdBindVertexBuffers(drawCmdBuffers[i], VERTEX_BUFFER_BIND_ID, 1, &models.skysphere.vertices.buffer, offsets);
			vkCmdBindIndexBuffer(drawCmdBuffers[i], models.skysphere.indices.buffer, 0, VK_INDEX_TYPE_UINT32);
			vkCmdDrawIndexed(drawCmdBuffers[i], models.skysphere.indexCount, 1, 0, 0, 0);

			DrawUI(drawCmdBuffers[i]);

			vkCmdEndRenderPass(drawCmdBuffers[i]);

			VK_CHECK_RESULT(vkEndCommandBuffer(drawCmdBuffers[i]));
		}
	}

	void LoadAssets()
	{
		models.plants.LoadFromFile(GetAssetPath() + "models/plants.dae", vertexLayout, 0.0025f, vulkanDevice, queue);
		models.ground.LoadFromFile(GetAssetPath() + "models/plane_circle.dae", vertexLayout, PLANT_RADIUS + 1.0f, vulkanDevice, queue);
		models.skysphere.LoadFromFile(GetAssetPath() + "models/skysphere.dae", vertexLayout, 512.0f / 10.0f, vulkanDevice, queue);

		// Textures
		std::string texFormatSuffix;
		VkFormat texFormat;
		// Get supported compressed texture format
		if (vulkanDevice->features.textureCompressionBC) {
			texFormatSuffix = "_bc3_unorm";
			texFormat = VK_FORMAT_BC3_UNORM_BLOCK;
		}
		else if (vulkanDevice->features.textureCompressionASTC_LDR) {
			texFormatSuffix = "_astc_8x8_unorm";
			texFormat = VK_FORMAT_ASTC_8x8_UNORM_BLOCK;
		}
		else if (vulkanDevice->features.textureCompressionETC2) {
			texFormatSuffix = "_etc2_unorm";
			texFormat = VK_FORMAT_ETC2_R8G8B8A8_UNORM_BLOCK;
		}
		else {
			vks::tools::ExitFatal("Device does not support any compressed texture format!", VK_ERROR_FEATURE_NOT_PRESENT);
		}

		textures.plants.LoadFromFile(GetAssetPath() + "textures/texturearray_plants" + texFormatSuffix + ".ktx", texFormat, vulkanDevice, queue);
		textures.ground.LoadFromFile(GetAssetPath() + "textures/ground_dry" + texFormatSuffix + ".ktx", texFormat, vulkanDevice, queue);
	}

	void SetupVertexDescriptions()
	{
		// Binding description
		vertices.bindingDescriptions.resize(2);

		// Mesh vertex buffer (description) at binding point 0
		vertices.bindingDescriptions[0] =
			vks::initializers::VertexInputBindingDescription(
				VERTEX_BUFFER_BIND_ID,
				vertexLayout.Stride(),
				// Input rate for the data passed to shader
				// Step for each vertex rendered
				VK_VERTEX_INPUT_RATE_VERTEX);

		vertices.bindingDescriptions[1] =
			vks::initializers::VertexInputBindingDescription(
				INSTANCE_BUFFER_BIND_ID,
				sizeof(InstanceData),
				// Input rate for the data passed to shader
				// Step for each instance rendered
				VK_VERTEX_INPUT_RATE_INSTANCE);

		// Attribute descriptions
		// Describes memory layout and shader positions
		vertices.attributeDescriptions.clear();

		// Per-Vertex attributes
		// Location 0 : Position
		vertices.attributeDescriptions.push_back(
			vks::initializers::VertexInputAttributeDescription(
				VERTEX_BUFFER_BIND_ID,
				0,
				VK_FORMAT_R32G32B32_SFLOAT,
				0)
		);
		// Location 1 : Normal
		vertices.attributeDescriptions.push_back(
			vks::initializers::VertexInputAttributeDescription(
				VERTEX_BUFFER_BIND_ID,
				1,
				VK_FORMAT_R32G32B32_SFLOAT,
				sizeof(float) * 3)
		);
		// Location 2 : Texture coordinates
		vertices.attributeDescriptions.push_back(
			vks::initializers::VertexInputAttributeDescription(
				VERTEX_BUFFER_BIND_ID,
				2,
				VK_FORMAT_R32G32_SFLOAT,
				sizeof(float) * 6)
		);
		// Location 3 : Color
		vertices.attributeDescriptions.push_back(
			vks::initializers::VertexInputAttributeDescription(
				VERTEX_BUFFER_BIND_ID,
				3,
				VK_FORMAT_R32G32B32_SFLOAT,
				sizeof(float) * 8)
		);

		// Instanced attributes
		// Location 4: Position
		vertices.attributeDescriptions.push_back(
			vks::initializers::VertexInputAttributeDescription(
				INSTANCE_BUFFER_BIND_ID, 4, VK_FORMAT_R32G32B32_SFLOAT, offsetof(InstanceData, pos))
		);
		// Location 5: Rotation
		vertices.attributeDescriptions.push_back(
			vks::initializers::VertexInputAttributeDescription(
				INSTANCE_BUFFER_BIND_ID, 5, VK_FORMAT_R32G32B32_SFLOAT, offsetof(InstanceData, rot))
		);
		// Location 6: Scale
		vertices.attributeDescriptions.push_back(
			vks::initializers::VertexInputAttributeDescription(
				INSTANCE_BUFFER_BIND_ID, 6, VK_FORMAT_R32_SFLOAT, offsetof(InstanceData, scale))
		);
		// Location 7: Texture array layer index
		vertices.attributeDescriptions.push_back(
			vks::initializers::VertexInputAttributeDescription(
				INSTANCE_BUFFER_BIND_ID, 7, VK_FORMAT_R32_SINT, offsetof(InstanceData, texIndex))
		);

		vertices.inputState = vks::initializers::PipelineVertexInputStateCreateInfo();
		vertices.inputState.vertexBindingDescriptionCount = static_cast<uint32_t>(vertices.bindingDescriptions.size());
		vertices.inputState.pVertexBindingDescriptions = vertices.bindingDescriptions.data();
		vertices.inputState.vertexAttributeDescriptionCount = static_cast<uint32_t>(vertices.attributeDescriptions.size());
		vertices.inputState.pVertexAttributeDescriptions = vertices.attributeDescriptions.data();
	}

	void SetupDescriptorPool()
	{
		// Example uses one ubo
		std::vector<VkDescriptorPoolSize> poolSizes =
		{
			vks::initializers::DescriptorPoolSize(VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1),
			vks::initializers::DescriptorPoolSize(VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 2),
		};

		VkDescriptorPoolCreateInfo descriptorPoolInfo =
			vks::initializers::DescriptorPoolCreateInfo(
				static_cast<uint32_t>(poolSizes.size()),
				poolSizes.data(),
				2);

		VK_CHECK_RESULT(vkCreateDescriptorPool(device, &descriptorPoolInfo, nullptr, &descriptorPool));
	}

	void SetupDescriptorSetLayout()
	{
		std::vector<VkDescriptorSetLayoutBinding> setLayoutBindings =
		{
			// Binding 0: Vertex shader uniform buffer
			vks::initializers::DescriptorSetLayoutBinding(
				VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
				VK_SHADER_STAGE_VERTEX_BIT,
				0),
			// Binding 1: Fragment shader combined sampler (plants texture array)
			vks::initializers::DescriptorSetLayoutBinding(
				VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
				VK_SHADER_STAGE_FRAGMENT_BIT,
				1),
			// Binding 1: Fragment shader combined sampler (ground texture)
			vks::initializers::DescriptorSetLayoutBinding(
				VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
				VK_SHADER_STAGE_FRAGMENT_BIT,
				2),
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

		std::vector<VkWriteDescriptorSet> writeDescriptorSets =
		{
			// Binding 0: Vertex shader uniform buffer
			vks::initializers::WriteDescriptorSet(
			descriptorSet,
				VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
				0,
				&uniformData.scene.descriptor),
			// Binding 1: Plants texture array combined
			vks::initializers::WriteDescriptorSet(
				descriptorSet,
				VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
				1,
				&textures.plants.descriptor),
			// Binding 2: Ground texture combined
			vks::initializers::WriteDescriptorSet(
				descriptorSet,
				VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
				2,
				&textures.ground.descriptor)
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
			VK_DYNAMIC_STATE_SCISSOR
		};
		VkPipelineDynamicStateCreateInfo dynamicState =
			vks::initializers::PipelineDynamicStateCreateInfo(
				dynamicStateEnables.data(),
				static_cast<uint32_t>(dynamicStateEnables.size()),
				0);

		VkGraphicsPipelineCreateInfo pipelineCreateInfo =
			vks::initializers::PipelineCreateInfo(
				pipelineLayout,
				renderPass,
				0);

		std::array<VkPipelineShaderStageCreateInfo, 2> shaderStages;

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

		// Indirect (and instanced) pipeline for the plants
		shaderStages[0] = LoadShader(GetShadersPath() + "indirectdraw/indirectdraw.vert.spv", VK_SHADER_STAGE_VERTEX_BIT);
		shaderStages[1] = LoadShader(GetShadersPath() + "indirectdraw/indirectdraw.frag.spv", VK_SHADER_STAGE_FRAGMENT_BIT);
		VK_CHECK_RESULT(vkCreateGraphicsPipelines(device, pipelineCache, 1, &pipelineCreateInfo, nullptr, &pipelines.plants));

		// Ground
		shaderStages[0] = LoadShader(GetShadersPath() + "indirectdraw/ground.vert.spv", VK_SHADER_STAGE_VERTEX_BIT);
		shaderStages[1] = LoadShader(GetShadersPath() + "indirectdraw/ground.frag.spv", VK_SHADER_STAGE_FRAGMENT_BIT);
		//rasterizationState.cullMode = VK_CULL_MODE_BACK_BIT;
		VK_CHECK_RESULT(vkCreateGraphicsPipelines(device, pipelineCache, 1, &pipelineCreateInfo, nullptr, &pipelines.ground));

		// Skysphere
		shaderStages[0] = LoadShader(GetShadersPath() + "indirectdraw/skysphere.vert.spv", VK_SHADER_STAGE_VERTEX_BIT);
		shaderStages[1] = LoadShader(GetShadersPath() + "indirectdraw/skysphere.frag.spv", VK_SHADER_STAGE_FRAGMENT_BIT);
		//rasterizationState.cullMode = VK_CULL_MODE_FRONT_BIT;
		VK_CHECK_RESULT(vkCreateGraphicsPipelines(device, pipelineCache, 1, &pipelineCreateInfo, nullptr, &pipelines.skysphere));
	}

	// Prepare (and stage) a buffer containing the indirect draw commands
	void PrepareIndirectData()
	{
		indirectCommands.clear();

		// Create on indirect command for each mesh in the scene
		uint32_t m = 0;
		for (auto& modelPart : models.plants.parts)
		{
			VkDrawIndexedIndirectCommand indirectCmd{};
			indirectCmd.instanceCount = OBJECT_INSTANCE_COUNT;
			indirectCmd.firstInstance = m * OBJECT_INSTANCE_COUNT;
			indirectCmd.firstIndex = modelPart.indexBase;
			indirectCmd.indexCount = modelPart.indexCount;

			indirectCommands.push_back(indirectCmd);

			m++;
		}

		indirectDrawCount = static_cast<uint32_t>(indirectCommands.size());

		objectCount = 0;
		for (auto indirectCmd : indirectCommands)
		{
			objectCount += indirectCmd.instanceCount;
		}

		vks::Buffer stagingBuffer;
		VK_CHECK_RESULT(vulkanDevice->CreateBuffer(
			VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
			VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
			&stagingBuffer,
			indirectCommands.size() * sizeof(VkDrawIndexedIndirectCommand),
			indirectCommands.data()));

		VK_CHECK_RESULT(vulkanDevice->CreateBuffer(
			VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
			VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
			&indirectCommandsBuffer,
			stagingBuffer.size));

		vulkanDevice->CopyBuffer(&stagingBuffer, &indirectCommandsBuffer, queue);

		stagingBuffer.Destroy();
	}

	// Prepare (and stage) a buffer containing instanced data for the mesh draws
	void PrepareInstanceData()
	{
		std::vector<InstanceData> instanceData;
		instanceData.resize(objectCount);

		std::default_random_engine rndEngine(benchmark.active ? 0 : (unsigned)time(nullptr));
		std::uniform_real_distribution<float> uniformDist(0.0f, 1.0f);

		for (uint32_t i = 0; i < objectCount; i++) {
			instanceData[i].rot = glm::vec3(0.0f, float(M_PI) * uniformDist(rndEngine), 0.0f);
			float theta = 2 * float(M_PI) * uniformDist(rndEngine);
			float phi = acos(1 - 2 * uniformDist(rndEngine));
			instanceData[i].pos = glm::vec3(sin(phi) * cos(theta), 0.0f, cos(phi)) * PLANT_RADIUS;
			instanceData[i].scale = 1.0f + uniformDist(rndEngine) * 2.0f;
			instanceData[i].texIndex = i / OBJECT_INSTANCE_COUNT;
		}

		vks::Buffer stagingBuffer;
		VK_CHECK_RESULT(vulkanDevice->CreateBuffer(
			VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
			VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
			&stagingBuffer,
			instanceData.size() * sizeof(InstanceData),
			instanceData.data()));

		VK_CHECK_RESULT(vulkanDevice->CreateBuffer(
			VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
			VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
			&instanceBuffer,
			stagingBuffer.size));

		vulkanDevice->CopyBuffer(&stagingBuffer, &instanceBuffer, queue);

		stagingBuffer.Destroy();
	}

	void PrepareUniformBuffers()
	{
		VK_CHECK_RESULT(vulkanDevice->CreateBuffer(
			VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
			VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
			&uniformData.scene,
			sizeof(uboVS)));

		VK_CHECK_RESULT(uniformData.scene.Map());

		UpdateUniformBuffer(true);
	}

	void UpdateUniformBuffer(bool viewChanged)
	{
		if (viewChanged)
		{
			uboVS.projection = camera.matrices.perspective;
			uboVS.view = camera.matrices.view;
		}

		memcpy(uniformData.scene.mapped, &uboVS, sizeof(uboVS));
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
		LoadAssets();
		PrepareIndirectData();
		PrepareInstanceData();
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
		{
			return;
		}
		Draw();
	}

	virtual void ViewChanged()
	{
		UpdateUniformBuffer(true);
	}

	virtual void OnUpdateUIOverlay(vks::UIOverlay* overlay)
	{
		if (!vulkanDevice->features.multiDrawIndirect) {
			if (overlay->Header("Info")) {
				overlay->Text("multiDrawIndirect not supported");
			}
		}
		if (overlay->Header("Statistics")) {
			overlay->Text("Objects: %d", objectCount);
		}
	}
};

VULKAN_EXAMPLE_MAIN(VulkanExampleIndirectDraw)