/*-------------------------------------------------------------------------
 * Vulkan CTS Framework
 * --------------------
 *
 * Copyright (c) 2015 Google Inc.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 *
 *//*!
 * \file
 * \brief Vulkan platform abstraction.
 *//*--------------------------------------------------------------------*/

#include "vkPlatform.hpp"
#include "tcuFunctionLibrary.hpp"
#ifdef CTS_USES_VULKANSC
#include "vkSafetyCriticalUtil.hpp"
#endif // CTS_USES_VULKANSC

namespace vk
{

PlatformDriver::PlatformDriver (const tcu::FunctionLibrary& library)
{
	m_vk.getInstanceProcAddr	= (GetInstanceProcAddrFunc)library.getFunction("vkGetInstanceProcAddr");

#define GET_PROC_ADDR(NAME) m_vk.getInstanceProcAddr(DE_NULL, NAME)
#include "vkInitPlatformFunctionPointers.inl"
#undef GET_PROC_ADDR
}

PlatformDriver::~PlatformDriver (void)
{
}

InstanceDriver::InstanceDriver (const PlatformInterface&		platformInterface,
								VkInstance						instance)
{
	loadFunctions(platformInterface, instance);
}

void InstanceDriver::loadFunctions (const PlatformInterface&	platformInterface,
									VkInstance					instance)
{
#define GET_PROC_ADDR(NAME) platformInterface.getInstanceProcAddr(instance, NAME)
#include "vkInitInstanceFunctionPointers.inl"
#undef GET_PROC_ADDR
}

InstanceDriver::~InstanceDriver (void)
{
}

#ifdef CTS_USES_VULKANSC

InstanceDriverSC::InstanceDriverSC (const PlatformInterface&				platformInterface,
									VkInstance								instance,
									const tcu::CommandLine&					cmdLine,
									de::SharedPtr<vk::ResourceInterface>	resourceInterface)
	: InstanceDriver(platformInterface, instance)
	, m_normalMode(cmdLine.isSubProcess())
	, m_resourceInterface(resourceInterface)
{
}

VkResult InstanceDriverSC::createDevice (VkPhysicalDevice						physicalDevice,
										 const VkDeviceCreateInfo*				pCreateInfo,
										 const VkAllocationCallbacks*			pAllocator,
										 VkDevice*								pDevice) const
{
	VkResult result = InstanceDriver::createDevice(physicalDevice, pCreateInfo, pAllocator, pDevice);
	if (result == VK_SUCCESS && !m_normalMode)
	{
		m_resourceInterface->registerDeviceFeatures(*pDevice, pCreateInfo);
	}
	return result;
}

#endif // CTS_USES_VULKANSC

DeviceDriver::DeviceDriver (const PlatformInterface&	platformInterface,
							VkInstance					instance,
							VkDevice					device)
{
	m_vk.getDeviceProcAddr = (GetDeviceProcAddrFunc)platformInterface.getInstanceProcAddr(instance, "vkGetDeviceProcAddr");

#define GET_PROC_ADDR(NAME) m_vk.getDeviceProcAddr(device, NAME)
#include "vkInitDeviceFunctionPointers.inl"
#undef GET_PROC_ADDR
}

DeviceDriver::~DeviceDriver (void)
{
}

#ifdef CTS_USES_VULKANSC
VkResult DeviceDriver::createShaderModule (VkDevice							device,
										   const VkShaderModuleCreateInfo*	pCreateInfo,
										   const VkAllocationCallbacks*		pAllocator,
										   VkShaderModule*					pShaderModule) const
{
	// this should not be called - Vulkan SC implementation uses DeviceDriverSC instead
	DE_UNREF(device);
	DE_UNREF(pCreateInfo);
	DE_UNREF(pAllocator);
	DE_UNREF(pShaderModule);
	TCU_THROW(InternalError, "Wrong DeviceDriver called in VulkanSC");
}
#endif

#ifdef CTS_USES_VULKANSC

DeviceDriverSC::DeviceDriverSC (const PlatformInterface&				platformInterface,
								VkInstance								instance,
								VkDevice								device,
								const tcu::CommandLine&					cmdLine,
								de::SharedPtr<vk::ResourceInterface>	resourceInterface)
	: DeviceDriver(platformInterface, instance, device)
	, m_normalMode(cmdLine.isSubProcess())
	, m_resourceInterface(resourceInterface)
{
	if(!cmdLine.isSubProcess())
		m_falseMemory.resize(64u * 1024u * 1024u, 0u);
	m_resourceInterface->initDevice(*this, device);
}

DeviceDriverSC::~DeviceDriverSC(void)
{
}

void DeviceDriverSC::destroyDeviceHandler (VkDevice								device,
										   const VkAllocationCallbacks*			pAllocator) const
{
	DE_UNREF(pAllocator);
	m_resourceInterface->unregisterDeviceFeatures(device);
}

VkResult DeviceDriverSC::createDescriptorSetLayoutHandlerNorm (VkDevice									device,
															   const VkDescriptorSetLayoutCreateInfo*	pCreateInfo,
															   const VkAllocationCallbacks*				pAllocator,
															   VkDescriptorSetLayout*					pSetLayout) const
{
	VkResult result = m_vk.createDescriptorSetLayout(device, pCreateInfo, pAllocator, pSetLayout);
	m_resourceInterface->registerObjectHash(pSetLayout->getInternal(), calculateDescriptorSetLayoutHash(*pCreateInfo, m_resourceInterface->getObjectHashes()));
	return result;
}

void DeviceDriverSC::createDescriptorSetLayoutHandlerStat (VkDevice									device,
														   const VkDescriptorSetLayoutCreateInfo*	pCreateInfo,
														   const VkAllocationCallbacks*				pAllocator,
														   VkDescriptorSetLayout*					pSetLayout) const
{
	DE_UNREF(device);
	DE_UNREF(pAllocator);

	DDSTAT_LOCK();
	DDSTAT_HANDLE_CREATE(descriptorSetLayoutRequestCount,1);
	DDSTAT_HANDLE_CREATE(descriptorSetLayoutBindingRequestCount, pCreateInfo->bindingCount);
	deUint32 immutableSamplersCount = 0u;
	for (deUint32 i = 0; i < pCreateInfo->bindingCount; ++i)
	{
		m_resourceInterface->getStatMax().descriptorSetLayoutBindingLimit = de::max(m_resourceInterface->getStatMax().descriptorSetLayoutBindingLimit, pCreateInfo->pBindings[i].binding + 1);
		if( ( pCreateInfo->pBindings[i].descriptorType == VK_DESCRIPTOR_TYPE_SAMPLER || pCreateInfo->pBindings[i].descriptorType == VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER )
			&& pCreateInfo->pBindings[i].pImmutableSamplers != DE_NULL )
			immutableSamplersCount += pCreateInfo->pBindings[i].descriptorCount;
	}
	m_resourceInterface->getStatMax().maxImmutableSamplersPerDescriptorSetLayout = de::max(m_resourceInterface->getStatMax().maxImmutableSamplersPerDescriptorSetLayout, immutableSamplersCount);

	*pSetLayout = VkDescriptorSetLayout(m_resourceInterface->incResourceCounter());
	m_descriptorSetLayouts.insert({ *pSetLayout, *pCreateInfo });
	m_resourceInterface->registerObjectHash(pSetLayout->getInternal(), calculateDescriptorSetLayoutHash(*pCreateInfo, m_resourceInterface->getObjectHashes()));
	m_resourceInterface->createDescriptorSetLayout(device, pCreateInfo, pAllocator, pSetLayout);
}

void DeviceDriverSC::destroyDescriptorSetLayoutHandler (VkDevice								device,
														VkDescriptorSetLayout					descriptorSetLayout,
														const VkAllocationCallbacks*			pAllocator) const
{
	DE_UNREF(device);
	DE_UNREF(pAllocator);

	DDSTAT_LOCK();
	auto it = m_descriptorSetLayouts.find(descriptorSetLayout);
	if (it != end(m_descriptorSetLayouts))
	{
		DDSTAT_HANDLE_DESTROY(descriptorSetLayoutRequestCount, 1);
		DDSTAT_HANDLE_DESTROY(descriptorSetLayoutBindingRequestCount, it->second.bindingCount);
		m_descriptorSetLayouts.erase(it);
	}
}

void DeviceDriverSC::createImageViewHandler (VkDevice						device,
											 const VkImageViewCreateInfo*	pCreateInfo,
											 const VkAllocationCallbacks*	pAllocator,
											 VkImageView*					pView) const
{
	DE_UNREF(device);
	DE_UNREF(pAllocator);

	DDSTAT_LOCK();
	DDSTAT_HANDLE_CREATE(imageViewRequestCount,1);
	if (pCreateInfo->subresourceRange.layerCount > 1)
		DDSTAT_HANDLE_CREATE(layeredImageViewRequestCount,1);
	m_resourceInterface->getStatMax().maxImageViewMipLevels		= de::max(m_resourceInterface->getStatMax().maxImageViewMipLevels, pCreateInfo->subresourceRange.levelCount);
	m_resourceInterface->getStatMax().maxImageViewArrayLayers	= de::max(m_resourceInterface->getStatMax().maxImageViewArrayLayers, pCreateInfo->subresourceRange.layerCount);
	if(pCreateInfo->subresourceRange.layerCount > 1)
		m_resourceInterface->getStatMax().maxLayeredImageViewMipLevels = de::max(m_resourceInterface->getStatMax().maxLayeredImageViewMipLevels, pCreateInfo->subresourceRange.levelCount);

	*pView = VkImageView(m_resourceInterface->incResourceCounter());
	m_imageViews.insert({ *pView, *pCreateInfo });
}

void DeviceDriverSC::destroyImageViewHandler (VkDevice						device,
											  VkImageView					imageView,
											  const VkAllocationCallbacks*	pAllocator) const
{
	DE_UNREF(device);
	DE_UNREF(pAllocator);

	DDSTAT_LOCK();
	auto it = m_imageViews.find(imageView);
	if (it != end(m_imageViews))
	{
		DDSTAT_HANDLE_DESTROY(imageViewRequestCount, 1);
		if(it->second.subresourceRange.layerCount > 1)
			DDSTAT_HANDLE_DESTROY(layeredImageViewRequestCount,1);
		m_imageViews.erase(it);
	}
}

void DeviceDriverSC::createQueryPoolHandler (VkDevice						device,
											 const VkQueryPoolCreateInfo*	pCreateInfo,
											 const VkAllocationCallbacks*	pAllocator,
											 VkQueryPool*					pQueryPool) const
{
	DE_UNREF(device);
	DE_UNREF(pAllocator);

	DDSTAT_LOCK();
	DDSTAT_HANDLE_CREATE(queryPoolRequestCount, 1);
	switch (pCreateInfo->queryType)
	{
		case VK_QUERY_TYPE_OCCLUSION:
			m_resourceInterface->getStatMax().maxOcclusionQueriesPerPool = de::max(m_resourceInterface->getStatMax().maxOcclusionQueriesPerPool, pCreateInfo->queryCount);
			break;
		case VK_QUERY_TYPE_PIPELINE_STATISTICS:
			m_resourceInterface->getStatMax().maxPipelineStatisticsQueriesPerPool = de::max(m_resourceInterface->getStatMax().maxPipelineStatisticsQueriesPerPool, pCreateInfo->queryCount);
			break;
		case VK_QUERY_TYPE_TIMESTAMP:
			m_resourceInterface->getStatMax().maxTimestampQueriesPerPool = de::max(m_resourceInterface->getStatMax().maxTimestampQueriesPerPool, pCreateInfo->queryCount);
			break;
		default:
			break;
	}
	// We don't have to track queryPool resources as we do with image views because they're not removed from memory in Vulkan SC.
	*pQueryPool = VkQueryPool(m_resourceInterface->incResourceCounter());
}

VkResult DeviceDriverSC::createPipelineLayoutHandlerNorm (VkDevice								device,
														  const VkPipelineLayoutCreateInfo*		pCreateInfo,
														  const VkAllocationCallbacks*			pAllocator,
														  VkPipelineLayout*						pPipelineLayout) const
{
	VkResult result = m_vk.createPipelineLayout(device, pCreateInfo, pAllocator, pPipelineLayout);
	m_resourceInterface->registerObjectHash(pPipelineLayout->getInternal(), calculatePipelineLayoutHash(*pCreateInfo, m_resourceInterface->getObjectHashes()));
	return result;
}

void DeviceDriverSC::createPipelineLayoutHandlerStat (VkDevice								device,
													  const VkPipelineLayoutCreateInfo*		pCreateInfo,
													  const VkAllocationCallbacks*			pAllocator,
													  VkPipelineLayout*						pPipelineLayout) const
{
	DDSTAT_LOCK();
	DDSTAT_HANDLE_CREATE(pipelineLayoutRequestCount, 1);
	*pPipelineLayout = VkPipelineLayout(m_resourceInterface->incResourceCounter());
	m_resourceInterface->registerObjectHash(pPipelineLayout->getInternal(), calculatePipelineLayoutHash(*pCreateInfo, m_resourceInterface->getObjectHashes()));
	m_resourceInterface->createPipelineLayout(device, pCreateInfo, pAllocator, pPipelineLayout);
}

VkResult DeviceDriverSC::createGraphicsPipelinesHandlerNorm (VkDevice								device,
															 VkPipelineCache						pipelineCache,
															 deUint32								createInfoCount,
															 const VkGraphicsPipelineCreateInfo*	pCreateInfos,
															 const VkAllocationCallbacks*			pAllocator,
															 VkPipeline*							pPipelines) const
{
	return m_resourceInterface->createGraphicsPipelines(device, pipelineCache, createInfoCount, pCreateInfos, pAllocator, pPipelines, m_normalMode);
}

void DeviceDriverSC::createGraphicsPipelinesHandlerStat (VkDevice									device,
														 VkPipelineCache							pipelineCache,
														 deUint32									createInfoCount,
														 const VkGraphicsPipelineCreateInfo*		pCreateInfos,
														 const VkAllocationCallbacks*				pAllocator,
														 VkPipeline*								pPipelines) const
{
	DE_UNREF(device);
	DE_UNREF(pipelineCache);
	DE_UNREF(pAllocator);

	DDSTAT_LOCK();
	DDSTAT_HANDLE_CREATE(graphicsPipelineRequestCount, createInfoCount);
	for (deUint32 i = 0; i < createInfoCount; ++i)
	{
		pPipelines[i] = VkPipeline(m_resourceInterface->incResourceCounter());
		m_graphicsPipelines.insert({ pPipelines[i], pCreateInfos[i] });
	}

	m_resourceInterface->createGraphicsPipelines(device, pipelineCache, createInfoCount, pCreateInfos, pAllocator, pPipelines, m_normalMode);
}

VkResult DeviceDriverSC::createComputePipelinesHandlerNorm (VkDevice								device,
															VkPipelineCache							pipelineCache,
															deUint32								createInfoCount,
															const VkComputePipelineCreateInfo*		pCreateInfos,
															const VkAllocationCallbacks*			pAllocator,
															VkPipeline*								pPipelines) const
{
	return m_resourceInterface->createComputePipelines(device, pipelineCache, createInfoCount, pCreateInfos, pAllocator, pPipelines, m_normalMode);
}

void DeviceDriverSC::createComputePipelinesHandlerStat (VkDevice									device,
														VkPipelineCache								pipelineCache,
														deUint32									createInfoCount,
														const VkComputePipelineCreateInfo*			pCreateInfos,
														const VkAllocationCallbacks*				pAllocator,
														VkPipeline*									pPipelines) const
{
	DE_UNREF(device);
	DE_UNREF(pipelineCache);
	DE_UNREF(pAllocator);

	DDSTAT_LOCK();
	DDSTAT_HANDLE_CREATE(computePipelineRequestCount, createInfoCount);
	for (deUint32 i = 0; i < createInfoCount; ++i)
	{
		pPipelines[i] = VkPipeline(m_resourceInterface->incResourceCounter());
		m_computePipelines.insert({ pPipelines[i], pCreateInfos[i] });
	}

	m_resourceInterface->createComputePipelines(device, pipelineCache, createInfoCount, pCreateInfos, pAllocator, pPipelines, m_normalMode);
}

void DeviceDriverSC::destroyPipelineHandler (VkDevice						device,
											 VkPipeline						pipeline,
											 const VkAllocationCallbacks*	pAllocator) const
{
	DE_UNREF(device);
	DE_UNREF(pAllocator);

	DDSTAT_LOCK();
	auto it = m_graphicsPipelines.find(pipeline);
	if (it != end(m_graphicsPipelines))
	{
		DDSTAT_HANDLE_DESTROY(graphicsPipelineRequestCount, 1);
		m_graphicsPipelines.erase(it);
		return;
	}

	auto it2 = m_computePipelines.find(pipeline);
	if (it2 != end(m_computePipelines))
	{
		DDSTAT_HANDLE_DESTROY(computePipelineRequestCount, 1);
		m_computePipelines.erase(it2);
	}
}

VkResult DeviceDriverSC::createRenderPassHandlerNorm (VkDevice						device,
													  const VkRenderPassCreateInfo*	pCreateInfo,
													  const VkAllocationCallbacks*	pAllocator,
													  VkRenderPass*					pRenderPass) const
{
	VkResult result = m_vk.createRenderPass(device, pCreateInfo, pAllocator, pRenderPass);
	m_resourceInterface->registerObjectHash(pRenderPass->getInternal(), calculateRenderPassHash(*pCreateInfo, m_resourceInterface->getObjectHashes()));
	return result;
}

void DeviceDriverSC::createRenderPassHandlerStat (VkDevice						device,
												  const VkRenderPassCreateInfo*	pCreateInfo,
												  const VkAllocationCallbacks*	pAllocator,
												  VkRenderPass*					pRenderPass) const
{
	DE_UNREF(device);
	DE_UNREF(pAllocator);

	DDSTAT_LOCK();
	DDSTAT_HANDLE_CREATE(renderPassRequestCount, 1);
	DDSTAT_HANDLE_CREATE(subpassDescriptionRequestCount,	pCreateInfo->subpassCount);
	DDSTAT_HANDLE_CREATE(attachmentDescriptionRequestCount,	pCreateInfo->attachmentCount);

	*pRenderPass = VkRenderPass(m_resourceInterface->incResourceCounter());
	m_renderPasses.insert({ *pRenderPass, *pCreateInfo });
	m_resourceInterface->registerObjectHash(pRenderPass->getInternal(), calculateRenderPassHash(*pCreateInfo, m_resourceInterface->getObjectHashes()));
	m_resourceInterface->createRenderPass(device, pCreateInfo, pAllocator, pRenderPass);
}

VkResult DeviceDriverSC::createRenderPass2HandlerNorm (VkDevice							device,
													   const VkRenderPassCreateInfo2*	pCreateInfo,
													   const VkAllocationCallbacks*		pAllocator,
													   VkRenderPass*					pRenderPass) const
{
	VkResult result = m_vk.createRenderPass2(device, pCreateInfo, pAllocator, pRenderPass);
	m_resourceInterface->registerObjectHash(pRenderPass->getInternal(), calculateRenderPass2Hash(*pCreateInfo, m_resourceInterface->getObjectHashes()));
	return result;
}

void DeviceDriverSC::createRenderPass2HandlerStat (VkDevice							device,
												   const VkRenderPassCreateInfo2*	pCreateInfo,
												   const VkAllocationCallbacks*		pAllocator,
												   VkRenderPass*					pRenderPass) const
{
	DE_UNREF(device);
	DE_UNREF(pAllocator);

	DDSTAT_LOCK();
	DDSTAT_HANDLE_CREATE(renderPassRequestCount, 1);
	DDSTAT_HANDLE_CREATE(subpassDescriptionRequestCount,	pCreateInfo->subpassCount);
	DDSTAT_HANDLE_CREATE(attachmentDescriptionRequestCount,	pCreateInfo->attachmentCount);

	*pRenderPass = VkRenderPass(m_resourceInterface->incResourceCounter());
	m_renderPasses2.insert({ *pRenderPass, *pCreateInfo });
	m_resourceInterface->registerObjectHash(pRenderPass->getInternal(), calculateRenderPass2Hash(*pCreateInfo, m_resourceInterface->getObjectHashes()));
	m_resourceInterface->createRenderPass2(device, pCreateInfo, pAllocator, pRenderPass);
}

void DeviceDriverSC::destroyRenderPassHandler (VkDevice						device,
											   VkRenderPass					renderPass,
											   const VkAllocationCallbacks*	pAllocator) const
{
	DE_UNREF(device);
	DE_UNREF(pAllocator);

	DDSTAT_LOCK();
	auto it = m_renderPasses.find(renderPass);
	if (it != end(m_renderPasses))
	{
		DDSTAT_HANDLE_DESTROY(renderPassRequestCount, 1);
		DDSTAT_HANDLE_DESTROY(subpassDescriptionRequestCount, it->second.subpassCount);
		DDSTAT_HANDLE_DESTROY(attachmentDescriptionRequestCount, it->second.attachmentCount);
		m_renderPasses.erase(it);
		return;
	}

	auto it2 = m_renderPasses2.find(renderPass);
	if (it2 != end(m_renderPasses2))
	{
		DDSTAT_HANDLE_DESTROY(renderPassRequestCount, 1);
		DDSTAT_HANDLE_DESTROY(subpassDescriptionRequestCount, it2->second.subpassCount);
		DDSTAT_HANDLE_DESTROY(attachmentDescriptionRequestCount, it2->second.attachmentCount);
		m_renderPasses2.erase(it2);
	}
}

VkResult DeviceDriverSC::createSamplerHandlerNorm (VkDevice							device,
												   const VkSamplerCreateInfo*		pCreateInfo,
												   const VkAllocationCallbacks*		pAllocator,
												   VkSampler*						pSampler) const
{
	VkResult result = m_vk.createSampler(device, pCreateInfo, pAllocator, pSampler);
	m_resourceInterface->registerObjectHash(pSampler->getInternal(), calculateSamplerHash(*pCreateInfo, m_resourceInterface->getObjectHashes()));
	return result;
}

void DeviceDriverSC::createSamplerHandlerStat (VkDevice							device,
											   const VkSamplerCreateInfo*		pCreateInfo,
											   const VkAllocationCallbacks*		pAllocator,
											   VkSampler*						pSampler) const
{
	DDSTAT_LOCK();
	DDSTAT_HANDLE_CREATE(samplerRequestCount, 1);
	*pSampler = VkSampler(m_resourceInterface->incResourceCounter());
	m_resourceInterface->registerObjectHash(pSampler->getInternal(), calculateSamplerHash(*pCreateInfo, m_resourceInterface->getObjectHashes()));
	m_resourceInterface->createSampler(device, pCreateInfo, pAllocator, pSampler);
}

VkResult DeviceDriverSC::createSamplerYcbcrConversionHandlerNorm (VkDevice								device,
																  const VkSamplerYcbcrConversionCreateInfo*	pCreateInfo,
																  const VkAllocationCallbacks*			pAllocator,
																  VkSamplerYcbcrConversion*				pYcbcrConversion) const
{
	VkResult result = m_vk.createSamplerYcbcrConversion(device, pCreateInfo, pAllocator, pYcbcrConversion);
	m_resourceInterface->registerObjectHash(pYcbcrConversion->getInternal(), calculateSamplerYcbcrConversionHash(*pCreateInfo, m_resourceInterface->getObjectHashes()));
	return result;
}

void DeviceDriverSC::createSamplerYcbcrConversionHandlerStat (VkDevice								device,
															  const VkSamplerYcbcrConversionCreateInfo*	pCreateInfo,
															  const VkAllocationCallbacks*			pAllocator,
															  VkSamplerYcbcrConversion*				pYcbcrConversion) const
{
	DDSTAT_LOCK();
	DDSTAT_HANDLE_CREATE(samplerYcbcrConversionRequestCount, 1);
	*pYcbcrConversion = VkSamplerYcbcrConversion(m_resourceInterface->incResourceCounter());
	m_resourceInterface->registerObjectHash(pYcbcrConversion->getInternal(), calculateSamplerYcbcrConversionHash(*pCreateInfo, m_resourceInterface->getObjectHashes()));
	m_resourceInterface->createSamplerYcbcrConversion(device, pCreateInfo, pAllocator, pYcbcrConversion);
}

void DeviceDriverSC::getDescriptorSetLayoutSupportHandler (VkDevice									device,
														   const VkDescriptorSetLayoutCreateInfo*	pCreateInfo,
														   VkDescriptorSetLayoutSupport*			pSupport) const
{
	DE_UNREF(device);

	DDSTAT_LOCK();
	for (deUint32 i = 0; i < pCreateInfo->bindingCount; ++i)
		m_resourceInterface->getStatMax().descriptorSetLayoutBindingLimit = de::max(m_resourceInterface->getStatMax().descriptorSetLayoutBindingLimit, pCreateInfo->pBindings[i].binding + 1);
	pSupport->supported = VK_TRUE;
}

VkResult DeviceDriverSC::createShaderModule (VkDevice							device,
											 const VkShaderModuleCreateInfo*	pCreateInfo,
											 const VkAllocationCallbacks*		pAllocator,
											 VkShaderModule*					pShaderModule) const
{
	DDSTAT_LOCK();
	return m_resourceInterface->createShaderModule(device, pCreateInfo, pAllocator, pShaderModule, m_normalMode);
}

VkResult DeviceDriverSC::createCommandPoolHandlerNorm (VkDevice								device,
													   const VkCommandPoolCreateInfo*		pCreateInfo,
													   const VkAllocationCallbacks*			pAllocator,
													   VkCommandPool*						pCommandPool) const
{
	vksc_server::VulkanCommandMemoryConsumption memC = m_resourceInterface->getNextCommandPoolSize();

	VkCommandPoolCreateInfo pCreateInfoCopy						= *pCreateInfo;

	VkCommandPoolMemoryReservationCreateInfo cpMemReservationCI	=
	{
		VK_STRUCTURE_TYPE_COMMAND_POOL_MEMORY_RESERVATION_CREATE_INFO,	// VkStructureType		sType
		DE_NULL,														// const void*			pNext
		memC.commandPoolReservedSize,									// VkDeviceSize			commandPoolReservedSize
		memC.commandBufferCount											// uint32_t				commandPoolMaxCommandBuffers
	};
	cpMemReservationCI.pNext									= pCreateInfoCopy.pNext;
	pCreateInfoCopy.pNext										= &cpMemReservationCI;

	return m_vk.createCommandPool(device, &pCreateInfoCopy, pAllocator, pCommandPool);
}

VkResult DeviceDriverSC::resetCommandPoolHandlerNorm (VkDevice								device,
													  VkCommandPool							commandPool,
													  VkCommandPoolResetFlags				flags) const
{
	return m_vk.resetCommandPool(device, commandPool, flags);
}

void DeviceDriverSC::createCommandPoolHandlerStat (VkDevice							device,
												   const VkCommandPoolCreateInfo*	pCreateInfo,
												   const VkAllocationCallbacks*		pAllocator,
												   VkCommandPool*					pCommandPool) const
{
	DE_UNREF(device);
	DE_UNREF(pCreateInfo);
	DE_UNREF(pAllocator);

	DDSTAT_LOCK();
	DDSTAT_HANDLE_CREATE(commandPoolRequestCount, 1);
	*pCommandPool = Handle<HANDLE_TYPE_COMMAND_POOL>(m_resourceInterface->incResourceCounter());
	m_resourceInterface->createCommandPool(device, pCreateInfo, pAllocator, pCommandPool);
}

void DeviceDriverSC::resetCommandPoolHandlerStat (VkDevice					device,
												  VkCommandPool				commandPool,
												  VkCommandPoolResetFlags	flags) const
{
	DE_UNREF(device);
	DE_UNREF(commandPool);
	DE_UNREF(flags);
}

void DeviceDriverSC::allocateCommandBuffersHandler (VkDevice								device,
													const VkCommandBufferAllocateInfo*		pAllocateInfo,
													VkCommandBuffer*						pCommandBuffers) const
{
	DDSTAT_LOCK();
	DDSTAT_HANDLE_CREATE(commandBufferRequestCount, pAllocateInfo->commandBufferCount);
	for (deUint32 i = 0; i < pAllocateInfo->commandBufferCount; ++i)
		pCommandBuffers[i] = (VkCommandBuffer)m_resourceInterface->incResourceCounter();
	m_resourceInterface->allocateCommandBuffers(device, pAllocateInfo, pCommandBuffers);
}

void DeviceDriverSC::freeCommandBuffersHandler (VkDevice								device,
												VkCommandPool							commandPool,
												deUint32								commandBufferCount,
												const VkCommandBuffer*					pCommandBuffers) const
{
	DE_UNREF(device);
	DE_UNREF(commandPool);
	DDSTAT_LOCK();
	for (deUint32 i = 0; i < commandBufferCount; ++i)
		if (pCommandBuffers[i] != DE_NULL)
			m_resourceInterface->getStatCurrent().commandBufferRequestCount -= 1;
}

void DeviceDriverSC::increaseCommandBufferSize (VkCommandBuffer	commandBuffer,
												const char*		functionName) const
{
	DDSTAT_LOCK();
	m_resourceInterface->increaseCommandBufferSize(commandBuffer, functionName);
}

de::SharedPtr<ResourceInterface> DeviceDriverSC::gerResourceInterface() const
{
	return m_resourceInterface;
}

void DeviceDriverSC::reset() const
{
	// these objects should be empty when function is invoked, but we will clear it anyway
	m_imageViews.clear();
	m_renderPasses.clear();
	m_renderPasses2.clear();
	m_graphicsPipelines.clear();
	m_computePipelines.clear();
}

#endif // CTS_USES_VULKANSC

#include "vkPlatformDriverImpl.inl"
#include "vkInstanceDriverImpl.inl"
#include "vkDeviceDriverImpl.inl"
#ifdef CTS_USES_VULKANSC
#include "vkDeviceDriverSCImpl.inl"
#endif // CTS_USES_VULKANSC

wsi::Display* Platform::createWsiDisplay (wsi::Type) const
{
	TCU_THROW(NotSupportedError, "WSI not supported");
}

bool Platform::hasDisplay (wsi::Type) const
{
	return false;
}

void Platform::describePlatform (std::ostream& dst) const
{
	dst << "vk::Platform::describePlatform() not implemented";
}

} // vk
