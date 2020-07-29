#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <vector>
#include <thread>
#include <random>

#define GLM_FORCE_RADIANS
#define GLM_FORCE_DEPTH_ZERO_TO_ONE
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

#include <vulkan/vulkan.h>
#include "VulkanBase.h"

#include "ThreadPool.hpp"
#include "Frustum.hpp"

#include "VulkanModel.hpp"

#define ENABLE_VALIDATION false
#define M_PI       3.14159265358979323846   // pi

class VulkanExampleMultiThreading : public VulkanBase
{
public:
	bool displaySkybox = true;

	// Vertex layout for the models
	vks::VertexLayout vertexLayout = vks::VertexLayout({
			vks::VERTEX_COMPONENT_POSITION,
			vks::VERTEX_COMPONENT_NORMAL,
			vks::VERTEX_COMPONENT_COLOR
	});

	struct  
	{
		vks::Model ufo;
		vks::Model skySphere;
	} models;

	// Shared matrices used for thread 
	// constant blocks
	struct  
	{
		glm::mat4 projection;
		glm::mat4 view;
	} matrices;

	struct  
	{
		VkPipeline phong;
		VkPipeline starSphere;
	} pipelines;

	VkPipelineLayout pipelineLayout;
	VkCommandBuffer primaryCommandBuffer;

	// Secondary scene command buffers used to store backdrop and user interface
	struct SecondaryCommandBuffers 
	{
		VkCommandBuffer background;
		VkCommandBuffer ui;
	} secondaryCommandBuffers;

	// Number of animated objects to be renderer
	// by using threads and secondary command buffers
	uint32_t numObjectsPerThread;

	// Multi-threaded stuff
	// Max. number of concurrent threads
	uint32_t numThreads;

	// Use push constants to update shader parameters on a per-thread base
	struct ThreadPushConstantBlock 
	{
		glm::mat4 mvp;
		glm::vec3 color;
	};

	struct ObjectData
	{
		glm::mat4 model;
		glm::vec3 pos;
		glm::vec3 rotation;
		float rotationDir;
		float rotationSpeed;
		float scale;
		float deltaT;
		float stateT;
		bool visible = true;
	};

	struct ThreadData
	{
		VkCommandPool commandPool;
		// One command buffer per render object
		std::vector<VkCommandBuffer> commandBuffer;
		// One push constant block per render object
		std::vector<ThreadPushConstantBlock> pushConstantBlock;
		// Per object information (position, rotation, etc.)
		std::vector<ObjectData> objectData;
	};

	std::vector<ThreadData> threadData;

	vks::ThreadPool threadPool;

	// Fence to wait for all command buffers to finish before presenting to the  swap chain
	VkFence renderFence = {};

	// Max. dimension of the ufo mesh for use as the sphere radius for frustum culling
	float objectSphereDim;

	// View frustum for culling invisible objects
	vks::Frustum frustum;

	std::default_random_engine rndEngine;

	VulkanExampleMultiThreading() : VulkanBase(ENABLE_VALIDATION)
	{
		title = "Multi threaded command buffer";
		camera.type = Camera::CameraType::lookat;
		camera.SetPosition(glm::vec3(0.0f, -0.0f, -32.5f));
		camera.SetRotation(glm::vec3(0.0f));
		camera.SetRotationSpeed(0.5f);
		camera.SetPerspective(60.0f, (float)width / (float)height, 0.1f, 256.0f);
		settings.overlay = true;
		// Get number of max. concurrrent threads
		numThreads = std::thread::hardware_concurrency();
		assert(numThreads > 0);
#if defined(__ANDROID__)
		LOGD("numThreads = %d", numThreads);
#else
		std::cout << "numThreads = " << numThreads << std::endl;
#endif
		threadPool.SetThreadCount(numThreads);
		numObjectsPerThread = 512 / numThreads;
		rndEngine.seed(benchmark.active ? 0 : (unsigned)time(nullptr));
	}

	~VulkanExampleMultiThreading()
	{
		// Clean up used Vulkan resources
		// Note : Inherited destructor cleans up resources stored in base class
		vkDestroyPipeline(device, pipelines.phong, nullptr);
		vkDestroyPipeline(device, pipelines.starSphere, nullptr);

		vkDestroyPipelineLayout(device, pipelineLayout, nullptr);

		models.ufo.Destroy();
		models.skySphere.Destroy();

		for (auto& thread : threadData)
		{
			vkFreeCommandBuffers(device, thread.commandPool, thread.commandBuffer.size(), thread.commandBuffer.data());
			vkDestroyCommandPool(device, thread.commandPool, nullptr);
		}

		vkDestroyFence(device, renderFence, nullptr);
	}

	float Rnd(float range)
	{
		std::uniform_real_distribution<float> rndDist(0.0f, range);
		return rndDist(rndEngine);
	}

	// Create all threads and initialize shader push constants
	void PrepareMultiThreadedRenderer()
	{
		// Since this demo updates the command buffers on each frame
		// we don't use the per-framebuffer command buffers from the base class,
		// and create a single primary command buffer instead
		VkCommandBufferAllocateInfo cmdBufAllocateInfo = vks::initializers::CommandBufferAllocateInfo(cmdPool, VK_COMMAND_BUFFER_LEVEL_PRIMARY, 1);
		VK_CHECK_RESULT(vkAllocateCommandBuffers(device, &cmdBufAllocateInfo, &primaryCommandBuffer));

		// Create additional secondary CBs for background and ui
		cmdBufAllocateInfo.level = VK_COMMAND_BUFFER_LEVEL_SECONDARY;
		VK_CHECK_RESULT(vkAllocateCommandBuffers(device, &cmdBufAllocateInfo, &secondaryCommandBuffers.background));
		VK_CHECK_RESULT(vkAllocateCommandBuffers(device, &cmdBufAllocateInfo, &secondaryCommandBuffers.ui));

		threadData.resize(numThreads);

		float maxX = std::floor(std::sqrt(numThreads * numObjectsPerThread));
		uint32_t posX = 0;
		uint32_t posZ = 0;

		for (uint32_t i = 0; i < numThreads; i++)
		{
			ThreadData* thread = &threadData[i];

			// Create one command pool for each thread
			VkCommandPoolCreateInfo cmdPoolInfo = vks::initializers::CommandPoolCreateInfo();
			cmdPoolInfo.queueFamilyIndex = swapChain.queueNodeIndex;
			cmdPoolInfo.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
			VK_CHECK_RESULT(vkCreateCommandPool(device, &cmdPoolInfo, nullptr, &thread->commandPool));

			// One secondary command buffer per object that is updated by this thread
			thread->commandBuffer.resize(numObjectsPerThread);
			// Generate secondary command buffer for each thread
			VkCommandBufferAllocateInfo secondaryCmdBufAllocateInfo = vks::initializers::CommandBufferAllocateInfo(thread->commandPool, VK_COMMAND_BUFFER_LEVEL_SECONDARY, thread->commandBuffer.size());
			VK_CHECK_RESULT(vkAllocateCommandBuffers(device, &secondaryCmdBufAllocateInfo, thread->commandBuffer.data()));

			thread->pushConstantBlock.resize(numObjectsPerThread);
			thread->objectData.resize(numObjectsPerThread);

			for (uint32_t j = 0; j < numObjectsPerThread; j++)
			{
				float theta = 2.0f * float(M_PI) * Rnd(1.0f);
				float phi = acos(1.0f - 2.0f * Rnd(1.0f));
				thread->objectData[j].pos = glm::vec3(sin(phi) * cos(theta), 0.0f, cos(phi)) * 35.0f;
				thread->objectData[j].rotation = glm::vec3(0.0f, Rnd(360.0f), 0.0f);
				thread->objectData[j].rotationDir = (Rnd(100.0f) < 50.0f) ? 1.0f : -1.0f;
				thread->objectData[j].rotationSpeed = (2.0f + Rnd(4.0f)) * thread->objectData[j].rotationDir;
				thread->objectData[j].scale = 0.75f + Rnd(0.5f);

				thread->pushConstantBlock[j].color = glm::vec3(Rnd(1.0f), Rnd(1.0f), Rnd(1.0f));
			}
		}
	}

	// Builds the secondary command buffer for each thread
	void ThreadRenderCode(uint32_t threadIndex, uint32_t cmdBufferIndex, VkCommandBufferInheritanceInfo inheritanceInfo)
	{
		ThreadData* thread = &threadData[threadIndex];
		ObjectData* objectData = &thread->objectData[cmdBufferIndex];

		// Check visibility against view frustum
		objectData->visible = frustum.CheckSphere(objectData->pos, objectSphereDim * 0.5f);

		if (!objectData->visible)
			return;

		VkCommandBufferBeginInfo commandBufferBeginInfo = vks::initializers::CommandBufferBeginInfo();
		commandBufferBeginInfo.flags = VK_COMMAND_BUFFER_USAGE_RENDER_PASS_CONTINUE_BIT;
		commandBufferBeginInfo.pInheritanceInfo = &inheritanceInfo;

		VkCommandBuffer cmdBuffer = thread->commandBuffer[cmdBufferIndex];

		VK_CHECK_RESULT(vkBeginCommandBuffer(cmdBuffer, &commandBufferBeginInfo));

		VkViewport viewport = vks::initializers::Viewport((float)width, (float)height, 0.0f, 1.0f);
		vkCmdSetViewport(cmdBuffer, 0, 1, &viewport);

		VkRect2D scissor = vks::initializers::Rect2D(width, height, 0, 0);
		vkCmdSetScissor(cmdBuffer, 0, 1, &scissor);

		vkCmdBindPipeline(cmdBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipelines.phong);

		// Update
		if (!paused)
		{
			objectData->rotation.y += 2.5f * objectData->rotationSpeed * frameTimer;
			if (objectData->rotation.y > 360.0f)
				objectData->rotation.y -= 360.0f;
			objectData->deltaT += 0.15f * frameTimer;
			if (objectData->deltaT > 1.0f)
				objectData->deltaT -= 1.0f;
			objectData->pos.y = sin(glm::radians(objectData->deltaT * 360.0f)) * 2.5f;
		}

		objectData->model = glm::translate(glm::mat4(1.0f), objectData->pos);
		objectData->model = glm::rotate(objectData->model, -sinf(glm::radians(objectData->deltaT * 360.0f)) * 0.25f, glm::vec3(objectData->rotationDir, 0.0f, 0.0f));
		objectData->model = glm::rotate(objectData->model, glm::radians(objectData->rotation.y), glm::vec3(0.0f, objectData->rotationDir, 0.0f));
		objectData->model = glm::rotate(objectData->model, glm::radians(objectData->deltaT * 360.0f), glm::vec3(0.0f, objectData->rotationDir, 0.0f));
		objectData->model = glm::scale(objectData->model, glm::vec3(objectData->scale));

		thread->pushConstantBlock[cmdBufferIndex].mvp = matrices.projection * matrices.view * objectData->model;

		// Update shader push constant block
		// Contains model view  matirx
		vkCmdPushConstants(cmdBuffer, pipelineLayout, VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(ThreadPushConstantBlock), &thread->pushConstantBlock[cmdBufferIndex]);

		VkDeviceSize offsets[1] = { 0 };
		vkCmdBindVertexBuffers(cmdBuffer, 0, 1, &models.ufo.vertices.buffer, offsets);
		vkCmdBindIndexBuffer(cmdBuffer, models.ufo.indices.buffer, 0, VK_INDEX_TYPE_UINT32);
		vkCmdDrawIndexed(cmdBuffer, models.ufo.indexCount, 1, 0, 0, 0);

		VK_CHECK_RESULT(vkEndCommandBuffer(cmdBuffer));
	}

	void UpdateSecondaryCommandBuffers(VkCommandBufferInheritanceInfo inheritanceInfo)
	{
		// Secondary command buffer for the sky sphere
		VkCommandBufferBeginInfo commandBufferBeginInfo = vks::initializers::CommandBufferBeginInfo();
		commandBufferBeginInfo.flags = VK_COMMAND_BUFFER_USAGE_RENDER_PASS_CONTINUE_BIT;
		commandBufferBeginInfo.pInheritanceInfo = &inheritanceInfo;

		VkViewport viewport = vks::initializers::Viewport((float)width, (float)height, 0.0f, 1.0f);
		VkRect2D scissor = vks::initializers::Rect2D(width, height, 0, 0);

		/*
			Backrground
		*/
		VK_CHECK_RESULT(vkBeginCommandBuffer(secondaryCommandBuffers.background, &commandBufferBeginInfo));

		vkCmdSetViewport(secondaryCommandBuffers.background, 0, 1, &viewport);
		vkCmdSetScissor(secondaryCommandBuffers.background, 0, 1, &scissor);

		vkCmdBindPipeline(secondaryCommandBuffers.background, VK_PIPELINE_BIND_POINT_GRAPHICS, pipelines.starSphere);

		glm::mat4 mvp = matrices.projection * matrices.view;
		mvp[3] = glm::vec4(0.0f, 0.0f, 0.0f, 1.0f);

		vkCmdPushConstants(secondaryCommandBuffers.background, pipelineLayout, VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(mvp), &mvp);

		VkDeviceSize offsets[1] = { 0 };
		vkCmdBindVertexBuffers(secondaryCommandBuffers.background, 0, 1, &models.skySphere.vertices.buffer, offsets);
		vkCmdBindIndexBuffer(secondaryCommandBuffers.background, models.skySphere.indices.buffer, 0, VK_INDEX_TYPE_UINT32);
		vkCmdDrawIndexed(secondaryCommandBuffers.background, models.skySphere.indexCount, 1, 0, 0, 0);

		VK_CHECK_RESULT(vkEndCommandBuffer(secondaryCommandBuffers.background));

		/*
			User interface

			With VK_SUBPASS_CONTENTS_SECONDARY_COMMAND_BUFFERS, the primary command buffer's content has to
			to defined by secondary command buffers, which also applies to the UI overlay command buffer
		*/
		VK_CHECK_RESULT(vkBeginCommandBuffer(secondaryCommandBuffers.ui, &commandBufferBeginInfo));

		vkCmdSetViewport(secondaryCommandBuffers.ui, 0, 1, &viewport);
		vkCmdSetScissor(secondaryCommandBuffers.ui, 0, 1, &scissor);

		vkCmdBindPipeline(secondaryCommandBuffers.ui, VK_PIPELINE_BIND_POINT_GRAPHICS, pipelines.starSphere);

		if (settings.overlay)
			DrawUI(secondaryCommandBuffers.ui);

		VK_CHECK_RESULT(vkEndCommandBuffer(secondaryCommandBuffers.ui));
	}

	// Updates the secondary command buffers using a thread pool
	// and puts them into the primary command buffer that's
	// lat submitted to the queue for rendering
	void UpdateCommandBuffers(VkFramebuffer frameBuffer)
	{
		// Contains the list of secondary command buffers to be submitted
		std::vector<VkCommandBuffer> commandBuffers;

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
		renderPassBeginInfo.framebuffer = frameBuffer;

		// Set target frame buffer

		VK_CHECK_RESULT(vkBeginCommandBuffer(primaryCommandBuffer, &cmdBufInfo));

		// The primary command buffer does not contain any rendering commands
		// These are stored (and retrieved) from the secondary command buffers
		vkCmdBeginRenderPass(primaryCommandBuffer, &renderPassBeginInfo, VK_SUBPASS_CONTENTS_SECONDARY_COMMAND_BUFFERS);

		// Inheritance info for the secondary command buffers
		VkCommandBufferInheritanceInfo inheritanceInfo = vks::initializers::CommandBufferInheritanceInfo();
		inheritanceInfo.renderPass = renderPass;
		// Secondary command buffer also use the currently active framebuffer
		inheritanceInfo.framebuffer = frameBuffer;

		// Update secondary scene command buffers
		UpdateSecondaryCommandBuffers(inheritanceInfo);

		if (displaySkybox)
			commandBuffers.emplace_back(secondaryCommandBuffers.background);

		// Add a job to the thread's queue for each object to be rendered
		for (auto t = 0; t < numThreads; t++)
		{
			for (auto i = 0; i < numObjectsPerThread; i++)
			{
				threadPool.threads[t]->AddJob([=] {
					ThreadRenderCode(t, i, inheritanceInfo);
				});
			}
		}

		threadPool.Wait();

		// Only submit if object is within the current view frustum
		for (auto t = 0; t < numThreads; t++)
		{
			for (auto i = 0; i < numObjectsPerThread; i++)
			{
				if (threadData[t].objectData[i].visible)
					commandBuffers.emplace_back(threadData[t].commandBuffer[i]);
			}
		}

		// Render ui last
		if (UIOverlay.visible)
			commandBuffers.emplace_back(secondaryCommandBuffers.ui);

		// Execute render commands from the secondary command buffer
		vkCmdExecuteCommands(primaryCommandBuffer, commandBuffers.size(), commandBuffers.data());
		vkCmdEndRenderPass(primaryCommandBuffer);
		VK_CHECK_RESULT(vkEndCommandBuffer(primaryCommandBuffer));
	}

	void LoadAssets()
	{
		models.ufo.LoadFromFile(GetAssetPath() + "models/retroufo_red_lowpoly.dae", vertexLayout, 0.12f, vulkanDevice, queue);
		models.skySphere.LoadFromFile(GetAssetPath() + "models/sphere.obj", vertexLayout, 1.0f, vulkanDevice, queue);
		objectSphereDim = std::max(std::max(models.ufo.dim.size.x, models.ufo.dim.size.y), models.ufo.dim.size.z);
	}

	void SetupPipelineLayout()
	{
		VkPipelineLayoutCreateInfo pPipelineLayoutCreateInfo =
			vks::initializers::PipelineLayoutCreateInfo(nullptr, 0);

		// Push constants for model matrices
		VkPushConstantRange pushConstantRange =
			vks::initializers::PushConstantRange(
				VK_SHADER_STAGE_VERTEX_BIT,
				sizeof(ThreadPushConstantBlock),
				0);

		// Push constant ranges are part of the pipeline layout
		pPipelineLayoutCreateInfo.pushConstantRangeCount = 1;
		pPipelineLayoutCreateInfo.pPushConstantRanges = &pushConstantRange;

		VK_CHECK_RESULT(vkCreatePipelineLayout(device, &pPipelineLayoutCreateInfo, nullptr, &pipelineLayout));
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
				VK_CULL_MODE_BACK_BIT,
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
				dynamicStateEnables.size(),
				0);

		std::array<VkPipelineShaderStageCreateInfo, 2> shaderStages;

		VkGraphicsPipelineCreateInfo pipelineCI = vks::initializers::PipelineCreateInfo(pipelineLayout, renderPass, 0);
		pipelineCI.pInputAssemblyState = &inputAssemblyState;
		pipelineCI.pRasterizationState = &rasterizationState;
		pipelineCI.pColorBlendState = &colorBlendState;
		pipelineCI.pMultisampleState = &multisampleState;
		pipelineCI.pViewportState = &viewportState;
		pipelineCI.pDepthStencilState = &depthStencilState;
		pipelineCI.pDynamicState = &dynamicState;
		pipelineCI.stageCount = shaderStages.size();
		pipelineCI.pStages = shaderStages.data();

		// Vertex bindings and attributes
		const std::vector<VkVertexInputBindingDescription> vertexInputBindings = {
			vks::initializers::VertexInputBindingDescription(0, vertexLayout.Stride(), VK_VERTEX_INPUT_RATE_VERTEX),
		};

		const std::vector<VkVertexInputAttributeDescription> vertexInputAttributes = {
			vks::initializers::VertexInputAttributeDescription(0, 0, VK_FORMAT_R32G32B32_SFLOAT, 0),					// Location 0: Position
			vks::initializers::VertexInputAttributeDescription(0, 1, VK_FORMAT_R32G32B32_SFLOAT, sizeof(float) * 3),	// Location 1: Normal
			vks::initializers::VertexInputAttributeDescription(0, 2, VK_FORMAT_R32G32B32_SFLOAT, sizeof(float) * 6),	// Location 2: Color
		};
		VkPipelineVertexInputStateCreateInfo vertexInputStateCI = vks::initializers::PipelineVertexInputStateCreateInfo();
		vertexInputStateCI.vertexBindingDescriptionCount = static_cast<uint32_t>(vertexInputBindings.size());
		vertexInputStateCI.pVertexBindingDescriptions = vertexInputBindings.data();
		vertexInputStateCI.vertexAttributeDescriptionCount = static_cast<uint32_t>(vertexInputAttributes.size());
		vertexInputStateCI.pVertexAttributeDescriptions = vertexInputAttributes.data();

		pipelineCI.pVertexInputState = &vertexInputStateCI;

		// Object rendering pipeline
		shaderStages[0] = LoadShader(GetShadersPath() + "multithreading/phong.vert.spv", VK_SHADER_STAGE_VERTEX_BIT);
		shaderStages[1] = LoadShader(GetShadersPath() + "multithreading/phong.frag.spv", VK_SHADER_STAGE_FRAGMENT_BIT);
		VK_CHECK_RESULT(vkCreateGraphicsPipelines(device, pipelineCache, 1, &pipelineCI, nullptr, &pipelines.phong));

		// Star sphere rendering pipeline
		rasterizationState.cullMode = VK_CULL_MODE_FRONT_BIT;
		depthStencilState.depthWriteEnable = VK_FALSE;
		shaderStages[0] = LoadShader(GetShadersPath() + "multithreading/starsphere.vert.spv", VK_SHADER_STAGE_VERTEX_BIT);
		shaderStages[1] = LoadShader(GetShadersPath() + "multithreading/starsphere.frag.spv", VK_SHADER_STAGE_FRAGMENT_BIT);
		VK_CHECK_RESULT(vkCreateGraphicsPipelines(device, pipelineCache, 1, &pipelineCI, nullptr, &pipelines.starSphere));
	}

	void UpdateMatrices()
	{
		matrices.projection = camera.matrices.perspective;
		matrices.view = camera.matrices.view;
		frustum.Update(matrices.projection * matrices.view);
	}

	void Draw()
	{
		// Wait for fence to signal that all command buffers are ready
		VkResult fenceRes;
		do {
			fenceRes = vkWaitForFences(device, 1, &renderFence, VK_TRUE, 100000000);
		} while (fenceRes == VK_TIMEOUT);
		VK_CHECK_RESULT(fenceRes);
		vkResetFences(device, 1, &renderFence);

		__super::PrepareFrame();

		UpdateCommandBuffers(frameBuffers[currentBuffer]);

		submitInfo.commandBufferCount = 1;
		submitInfo.pCommandBuffers = &primaryCommandBuffer;

		VK_CHECK_RESULT(vkQueueSubmit(queue, 1, &submitInfo, renderFence));

		__super::SubmitFrame();
	}

	void Prepare()
	{
		__super::Prepare();
		// Create a fence for synchronization
		VkFenceCreateInfo fenceCreateInfo = vks::initializers::FenceCreateInfo(VK_FENCE_CREATE_SIGNALED_BIT);
		vkCreateFence(device, &fenceCreateInfo, nullptr, &renderFence);
		LoadAssets();
		SetupPipelineLayout();
		PreparePipelines();
		PrepareMultiThreadedRenderer();
		UpdateMatrices();
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
		UpdateMatrices();
	}

	virtual void OnUpdateUIOverlay(vks::UIOverlay* overlay)
	{
		if (overlay->Header("Statistics")) {
			overlay->Text("Active threads: %d", numThreads);
		}
		if (overlay->Header("Settings")) {
			overlay->CheckBox("Skybox", &displaySkybox);
		}

	}
};

VULKAN_EXAMPLE_MAIN(VulkanExampleMultiThreading)