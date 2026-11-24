#include <volk.h>
#include <vulkan/vulkan_core.h>

#define GLM_FORCE_RADIANS
#define GLM_FORCE_DEPTH_ZERO_TO_ONE
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

#include <imgui.h>

#include "core/check_error.hpp"
#include "core/filesystem.hpp"
#include "core/vulkan_app.hpp"

#include "common/buffer.hpp"
#include "common/image.hpp"
#include "common/scene.hpp"

#include <array>
#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>

//--------------------------------------------------------------------------------------------------

constexpr uint32_t WIDTH = 1920;
constexpr uint32_t HEIGHT = 1080;
constexpr bool VSYNC = true;

// TODO: Assignment 2.6 / 2.9
// Update the model to be loaded here.
const std::string SCENE = //
    ""
    //"assets/shiba.glb"
    "assets/tokyo.glb";

//--------------------------------------------------------------------------------------------------

struct VertexPushConstants {
    glm::mat4 MVP;
};

struct MaterialUniformBuffer {
    glm::vec4 baseColor;
    int32_t hasTexture;
    float alphacutoff;
};

//--------------------------------------------------------------------------------------------------

class Assignment final {
  public:
    void init() {
        VK_CHECK(m_app.init("Crat3r", WIDTH, HEIGHT, VSYNC));

        if (!SCENE.empty()) {
            if (!m_scene.load(SCENE)) {
                fprintf(stderr, "Loading of scene failed!");
                VK_CHECK(VK_ERROR_UNKNOWN);
            }

            m_app.setSceneSize(m_scene.sizeWorld);
            m_app.setCameraAngles(0.47f, 4.08f, m_scene.sizeWorld);
        } else {
            m_app.setCameraAngles(0.57f, 0.96f, 2.f);
        }

        createRenderPass();
        createDescriptorSetLayout();
        createPipelineLayout();
        createPipeline();

        createDepthResources();
        createFramebuffer();

        createVertexBuffers();
        createIndexBuffers();

        createTextures();
        createTextureSampler();

        createUniformBuffers();

        createDescriptorPool();
        createDescriptorSets();
    }

    void run() {
        m_app.run(
            [&]() { // onRenderUi
                buildUi();
            },
            [&](VkCommandBuffer commandBuffer) { // onRender
                m_scene.updateAnimationState(m_app.getDeltaTime());
                recordCommandBuffer(commandBuffer);
            },
            [&]() { // onResize
                freeDepthResources();
                freeFramebuffer();

                createDepthResources();
                createFramebuffer();
            }
        );
    }

    void deinit() {
        const VkDevice device = m_app.getDevice();
        const VmaAllocator allocator = m_app.getAllocator();

        // TODO: Assignment 2.x
        // Destroy and free any ressources you have created.
        vkDestroyRenderPass(device, m_renderPass, nullptr);
        vkDestroyPipelineLayout(device, m_pipelineLayout, nullptr);
        vkDestroyPipeline(device, m_pipeline, nullptr);
        for (auto& buffer : m_vertexBuffers) {
            buffer.deinit(allocator);
        }
        for (auto& buffer : m_indexBuffers) {
            buffer.deinit(allocator);
        }
        freeDepthResources();
        freeFramebuffer();
        vkDestroyDescriptorPool(device, m_descriptorPool, nullptr);
        vkDestroyDescriptorSetLayout(device, m_descriptorSetLayout, nullptr);
        vkDestroySampler(device, m_textureSampler, nullptr);
        for (VkImageView view : m_textureImageViews) {
            vkDestroyImageView(device, view, nullptr);
        }

        for (auto& image : m_textureImages) {
            image.deinit(allocator);
        }

        for (auto& buffer : m_materialUniformBuffers) {
            buffer.deinit(allocator);
        }

        m_app.deinit();
    }

  private:
    VulkanApp m_app;
    Scene m_scene;

    // TODO: Assignment 2.x
    // Add any other necessary class members here.
    VkRenderPass m_renderPass = VK_NULL_HANDLE;
    VkPipelineLayout m_pipelineLayout = VK_NULL_HANDLE;
    VkPipeline m_pipeline = VK_NULL_HANDLE;
    VkFramebuffer m_framebuffer = VK_NULL_HANDLE;
    std::vector<Buffer> m_vertexBuffers;
    std::vector<Buffer> m_indexBuffers;
    Image m_depthImage{};
    VkImageView m_depthImageView = VK_NULL_HANDLE;

    std::vector<Image> m_textureImages;
    std::vector<VkImageView> m_textureImageViews;
    VkSampler m_textureSampler = VK_NULL_HANDLE;

    VkDescriptorSetLayout m_descriptorSetLayout = VK_NULL_HANDLE;
    VkDescriptorPool m_descriptorPool = VK_NULL_HANDLE;
    std::vector<VkDescriptorSet> m_materialDescriptorSets;

    std::vector<Buffer> m_materialUniformBuffers;

  private:
    //--------------------------------------------------------------------------------------------------

    void createRenderPass() {
        const VkFormat targetFormat = m_app.getRenderTargetFormat();

        // TODO: Assignment 2.1
        // First, create a render pass for our rasterizer. This creation info struct references a few other
        // descriptions.
        //
        // Start with a 'VkAttachmentDescription' for the color attachment. The format of the image that we are
        // going to render to is specified by 'targetFormat'. Importantly, set the load operation to CLEAR and the
        // store operation to STORE. We don't care about the stencil operations for this attachment. Set the initial
        // and final layouts to the correct values for the image before and after the render pass.
        //
        // Next, we will create a single subpass for our rendering, that uses the color attachment.
        // So, create a 'VkAttachmentReference' for the first attachment in the optimal layout for color attachment.
        // Then, initalize a graphics subpass with 'VkSubpassDescription' using the attachment reference.
        //
        // To ensure that the image is ready to be written to before the render pass starts, we will create a
        // subpass dependency. In this case, the dependency will be between the previous and current render passes,
        // rather than between subpasses. Fill a 'VkSubpassDependency' with a source subpass of 'VK_SUBPASS_EXTERNAL'.
        // The destination is the first subpass. Set both the 'dstStageMask' and 'dstAccessMask' so that the
        // image can be written as a color attachment. There are multiple options for the source masks; find one
        // that works. Tip: Before rendering, the image is read by a shader in the previous rendering iteration.
        //
        // Finally, create the render pass with the color attachment, the subpass and dependency.
        //
        // [ New functions: vkCreateRenderPass ]

        const std::array<VkAttachmentDescription, 2> attachments{{
            {
                // Framebuffer
                .format = targetFormat,
                .samples = VK_SAMPLE_COUNT_1_BIT,
                .loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR,   // Verwerfen des alten Inhalts; Bild hat nur Clear Value
                .storeOp = VK_ATTACHMENT_STORE_OP_STORE, // Speichern des Inhalts zum anzeigen
                .stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE,   // verwenden wir nicht
                .stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE, // verwenden wir nicht
                .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
                .finalLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
            },
            {// DepthBuffer
             .format = VK_FORMAT_D32_SFLOAT,
             .samples = VK_SAMPLE_COUNT_1_BIT,
             .loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR,            // Verwerfen des alten Inhalts; Bild hat nur Clear Value
             .storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE,      // die Tiefenwerte brauchen wir nur innerhalb des passes
             .stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE, // verwenden wir nicht
             .stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE, // verwenden wir nicht
             .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
             .finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL
            },
        }};

        const VkAttachmentReference colorAttachReference{
            .attachment = 0, .layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL
        };

        const VkAttachmentReference depthAttachReference{
            .attachment = 1,
            .layout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL,
        };

        const VkSubpassDescription subPasses{
            // Ein Subpass = Zeichenabschnitt
            .pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS,
            .colorAttachmentCount = 1,
            .pColorAttachments = &colorAttachReference,
            .pDepthStencilAttachment = &depthAttachReference,
        };

        // [0] Color, WAR: UI hat das Bild im vorigen Frame gesampelt -> vor dem Schreiben warten (srcAccess = 0)
        // [1] Depth, WAW: voriger Frame hat Tiefen geschrieben, unser Clear schreibt auch (VL 04, Folie 70)
        // Ausgehende Dependency (0 -> EXTERNAL) ergänzt Vulkan implizit (VL 04, Folie 67)
        const std::array<VkSubpassDependency, 2> dependencies{
            {{.srcSubpass = VK_SUBPASS_EXTERNAL,
              .dstSubpass = 0,
              .srcStageMask = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
              .dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
              .srcAccessMask = 0,
              .dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT},
             {
                 .srcSubpass = VK_SUBPASS_EXTERNAL,
                 .dstSubpass = 0,
                 .srcStageMask = VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT,
                 .dstStageMask = VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT,
                 .srcAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT,
                 .dstAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT,
             }}
        };

        const VkRenderPassCreateInfo renderPassInfo{
            .sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO,
            .attachmentCount = attachments.size(),
            .pAttachments = attachments.data(),
            .subpassCount = 1,
            .pSubpasses = &subPasses,
            .dependencyCount = dependencies.size(),
            .pDependencies = dependencies.data(),
        };

        VK_CHECK(vkCreateRenderPass(m_app.getDevice(), &renderPassInfo, nullptr, &m_renderPass));

        // TODO: Assignment 2.7
        // Add a second attachment description and attachment reference and subpass dependency
        // for the depth attachment.
    }

    void createDescriptorSetLayout() {
        // TODO: Assignment 2.8
        // Create a descriptor set layout for a single combined image sampler at binding zero.
        const VkDescriptorSetLayoutBinding textureBindings[] = {
            {
                .binding = 0,
                .descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
                .descriptorCount = 1,
                .stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT,
            },
            {.binding = 1,
             .descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
             .descriptorCount = 1,
             .stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT}
        };

        const VkDescriptorSetLayoutCreateInfo layoutInfo{
            .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
            .bindingCount = 2,
            .pBindings = textureBindings,
        };

        VK_CHECK(vkCreateDescriptorSetLayout(m_app.getDevice(), &layoutInfo, nullptr, &m_descriptorSetLayout));

        // TODO: Assignment 2.12
        // Add a second layout binding for a uniform buffer.
    }

    void createPipelineLayout() {
        // TODO: Assignment 2.1
        // Create a simple pipeline layout for zero descriptor sets and push constant ranges.
        //
        // [ New functions: vkCreatePipelineLayout ]

        const VkPushConstantRange pConstants{
            .stageFlags = VK_SHADER_STAGE_VERTEX_BIT,
            .offset = 0,
            .size = sizeof(VertexPushConstants),
        };

        const VkPipelineLayoutCreateInfo layoutInfo{
            .sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
            .setLayoutCount = 1,
            .pSetLayouts = &m_descriptorSetLayout,
            .pushConstantRangeCount = 1,
            .pPushConstantRanges = &pConstants
        };

        VK_CHECK(vkCreatePipelineLayout(m_app.getDevice(), &layoutInfo, nullptr, &m_pipelineLayout));

        // TODO: Assignment 2.4
        // Add a push constant range for the vertex shader using the 'VertexPushConstants' struct.

        // TODO: Assignment 2.8
        // Add the descriptor set layout to the pipeline layout.
    }

    static VkShaderModule createShaderModule(VkDevice device, const std::vector<char>& code) {
        const VkShaderModuleCreateInfo createInfo{
            .sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,
            .codeSize = code.size(),
            .pCode = reinterpret_cast<const uint32_t*>(code.data()),
        };

        VkShaderModule shaderModule;
        VK_CHECK(vkCreateShaderModule(device, &createInfo, nullptr, &shaderModule));

        return shaderModule;
    }

    void createPipeline() {
        const VkDevice device = m_app.getDevice();

        const auto vertShaderCode = Fs::readFile("shaders/a02/simple.vert.spv");
        const auto fragShaderCode = Fs::readFile("shaders/a02/simple.frag.spv");

        const VkShaderModule vertShaderModule = createShaderModule(m_app.getDevice(), vertShaderCode);
        const VkShaderModule fragShaderModule = createShaderModule(m_app.getDevice(), fragShaderCode);

        // TODO: Assignment 2.1
        // Now it is time to create the graphics pipeline. The 'VkGraphicsPipelineCreateInfo' references several
        // other create infos. Refer to the documentation for each create info to understand its purpose.
        // Here are a few tips to help you fill them out:
        // - We need two stages: the vertex and fragment shaders. The entry function name for both is "main".
        // - There is no vertex input for now, so the binding and attribute descriptions can be set to zero.
        // - The primitives should be assembled into a list of triangles.
        // - As we are not using tesselation, the corresponding state can be a nullptr.
        // - The viewports and scissors can be baked into the pipeline. However, to enable resizing of the
        //   surface without rebuilding the pipeline, they can be set dynamically using dynamic states. To do this,
        //   first set the viewport and scissor pointers in the viewport create info to nullptr, while setting
        //   their counts to one. Then, in the dynamic state info, enable the two corresponding dynamic states.
        // - The polygons should be drawn as filled. Disable all other rasterization options for now.
        // - Even though we are not using multisampling, the corresponding state still has to be specified.
        //   Set the sample count to one and disable sample shading to turn off multisampling.
        // - You have to specify color blending for the one color attachment. Blending can be disabled for now;
        //   however, do not forget to enable all color components in the write mask or you will not see anything!
        // If you fill all structs correctly, the pipeline creation should succeed without any complaints from
        // the validation layers. The pipeline cache can be set to 'VK_NULL_HANDLE' again.
        //
        // [ New functions: vkCreateGraphicsPipelines ]

        const std::array<VkPipelineShaderStageCreateInfo, 2> shaderStages{
            {{.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
              .stage = VK_SHADER_STAGE_VERTEX_BIT,
              .module = vertShaderModule,
              .pName = "main"},
             {.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
              .stage = VK_SHADER_STAGE_FRAGMENT_BIT,
              .module = fragShaderModule,
              .pName = "main"}}
        };

        const VkVertexInputBindingDescription vertexBinding{
            .binding = 0,
            .stride = sizeof(Scene::Vertex),
            .inputRate =
                VK_VERTEX_INPUT_RATE_VERTEX, // Position und UV-Koordinaten werden pro Vertex aus dem Buffer gelesen.
        };

        const VkVertexInputAttributeDescription vertexAttributes[] = {
            {
                .location = 0,
                .binding = 0,
                .format = VK_FORMAT_R32G32B32_SFLOAT, // Vec3 a 32bit
                .offset = offsetof(Scene::Vertex, pos),
            },
            {
                .location = 1,
                .binding = 0,
                .format = VK_FORMAT_R32G32_SFLOAT, // vec2 a 32bit
                .offset = offsetof(Scene::Vertex, uv),
            },
        };

        const VkPipelineVertexInputStateCreateInfo vertexInputInfo{
            .sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO,
            .vertexBindingDescriptionCount = 1,
            .pVertexBindingDescriptions = &vertexBinding,
            .vertexAttributeDescriptionCount = 2,
            .pVertexAttributeDescriptions = vertexAttributes,
        };

        const VkPipelineInputAssemblyStateCreateInfo inputAssembly{
            // legt fest, wie Vulkan die Vertices zu geometrischen Formen zusammensetzt
            .sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO,
            .topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST,
        };

        const VkPipelineViewportStateCreateInfo viewportState{
            .sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO,
            .viewportCount = 1,
            .pViewports = nullptr,
            .scissorCount = 1,
            .pScissors = nullptr
        };

        const VkPipelineRasterizationStateCreateInfo rasterization{
            .sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO,
            .depthClampEnable = VK_FALSE,        // Dreiecke hinter 1.0 werden abgeschnitten
            .rasterizerDiscardEnable = VK_FALSE, // Behalten der Dreiecke für den Frag Shader
            .polygonMode = VK_POLYGON_MODE_FILL,
            .cullMode = VK_CULL_MODE_BACK_BIT,
            .frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE,
            .depthBiasEnable = VK_FALSE,
            .depthBiasConstantFactor = 0,
            .depthBiasClamp = 0,
            .depthBiasSlopeFactor = 0,
            .lineWidth = 1.0f // Validation Layer
        };

        const VkPipelineMultisampleStateCreateInfo multisampling{
            .sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO,
            .rasterizationSamples = VK_SAMPLE_COUNT_1_BIT,
            .sampleShadingEnable = VK_FALSE
        };

        const VkPipelineColorBlendAttachmentState colorAttachment{
            .blendEnable = VK_TRUE,
            .srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA,           // Multipliziert neue farbe mit src.a
            .dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA, // Multipliziert bisherige Farbe mit 1-src.a
            .colorBlendOp = VK_BLEND_OP_ADD,                            // Addieren => Alpha Blending Formel
            .srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE,                 // Quellbeitrag: src.a * 1
            .dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA, // Zielbeitrag: dst.a * (1 - src.a)
            .alphaBlendOp = VK_BLEND_OP_ADD,                            // out.a = src.a + dst.a * (1 - src.a)
            .colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT |
                              VK_COLOR_COMPONENT_A_BIT,
        };

        const VkPipelineColorBlendStateCreateInfo colorBlending{
            .sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO,
            .attachmentCount = 1,
            .pAttachments = &colorAttachment,
        };

        const VkDynamicState dynamicStates[] = {
            VK_DYNAMIC_STATE_VIEWPORT,
            VK_DYNAMIC_STATE_SCISSOR,
        };

        const VkPipelineDynamicStateCreateInfo dynamicState{
            .sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO,
            .dynamicStateCount = 2,
            .pDynamicStates = dynamicStates,
        };

        const VkPipelineDepthStencilStateCreateInfo depthStencil{
            .sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO,
            .depthTestEnable = VK_TRUE,
            .depthWriteEnable = VK_TRUE,
            .depthCompareOp = VK_COMPARE_OP_LESS,
            .depthBoundsTestEnable = VK_FALSE,
            .stencilTestEnable = VK_FALSE,
        };

        const VkGraphicsPipelineCreateInfo pipelineInfo{
            .sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO,
            .stageCount = shaderStages.size(),
            .pStages = shaderStages.data(),
            .pVertexInputState = &vertexInputInfo,
            .pInputAssemblyState = &inputAssembly,
            .pTessellationState = nullptr,
            .pViewportState = &viewportState,
            .pRasterizationState = &rasterization,
            .pMultisampleState = &multisampling,
            .pDepthStencilState = &depthStencil,
            .pColorBlendState = &colorBlending,
            .pDynamicState = &dynamicState,
            .layout = m_pipelineLayout,
            .renderPass = m_renderPass,
            .subpass = 0
        };

        VK_CHECK(vkCreateGraphicsPipelines(device, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &m_pipeline));

        // TODO: Assignment 2.2
        // Update the vertex input state to enable the upload of vertex data to the rasterizer. We will use one
        // vertex binding, which is described on the CPU by 'Scene::Vertex'. One of these vertices contains two
        // attributes: a three-dimensional position and a two-dimensional UV coordiate. Both of these use 32-bit
        // signed floating-point numbers. You can use 'offsetof(Scene::Vertex, pos)' to find the offset of a
        // field in a struct.

        // TODO: Assignment 2.5
        // Enable back-face culling in the rasterization state. Note that here, by convention, the front faces
        // are oriented counter-clockwise.

        // TODO: Assignment 2.7
        // Add a depth and stencil state and enable the writing of depth values and depth test. Also specify
        // the depth compare operation. The stencil operations should be disabled.

        // TODO: Assignment 2.11
        // Enable color blending for this pipeline. Set the blend operations and factors for standard
        // alpha blending.

        vkDestroyShaderModule(m_app.getDevice(), fragShaderModule, nullptr);
        vkDestroyShaderModule(m_app.getDevice(), vertShaderModule, nullptr);
    }

    void createDepthResources() {
        const VkFormat format = VK_FORMAT_D32_SFLOAT;
        const VkExtent2D extent = m_app.getRenderTargetExtent();

        // TODO: Assignment 2.7
        // Create a device-local image of the specified format and size to be used as a depth attachment.
        // See 'image.hpp' for a convenient helper struct. Then create an image view of the depth image.

        VK_CHECK(m_depthImage.init(
            m_app.getAllocator(),
            VK_IMAGE_TYPE_2D,
            format,
            {extent.width, extent.height, 1},
            VK_SAMPLE_COUNT_1_BIT,
            VK_IMAGE_TILING_OPTIMAL,
            VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT,
            VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT
        ));

        const VkImageViewCreateInfo viewInfo{
            .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
            .image = m_depthImage.image,
            .viewType = VK_IMAGE_VIEW_TYPE_2D,
            .format = format,
            .components = {VK_COMPONENT_SWIZZLE_IDENTITY}, // 0 => RGBA Werte werden nicht verschoben
            .subresourceRange = {
                .aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT,
                .baseMipLevel = 0,
                .levelCount = 1,
                .baseArrayLayer = 0,
                .layerCount = 1,
            }
        };

        VK_CHECK(vkCreateImageView(m_app.getDevice(), &viewInfo, nullptr, &m_depthImageView))
    }

    void freeDepthResources() {
        // TODO: Assignment 2.7
        // Destroy the depth image view and free the depth image.
        vkDestroyImageView(m_app.getDevice(), m_depthImageView, nullptr);
        m_depthImageView = VK_NULL_HANDLE;
        m_depthImage.deinit(m_app.getAllocator());
    }

    void createFramebuffer() {
        const VkImageView colorAttachment = m_app.getRenderTargetImageView();
        const VkExtent2D imageExtent = m_app.getRenderTargetExtent();

        // TODO: Assignment 2.1
        // Create a framebuffer that contains the target color image view as a single attachment.
        //
        // [ New functions: vkCreateFramebuffer ]

        const std::array<VkImageView, 2> attachments = {colorAttachment, m_depthImageView};

        const VkFramebufferCreateInfo framebufferInfo{
            .sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO,
            .renderPass = m_renderPass,
            .attachmentCount = attachments.size(),
            .pAttachments = attachments.data(),
            .width = imageExtent.width,
            .height = imageExtent.height,
            .layers = 1
        };

        VK_CHECK(vkCreateFramebuffer(m_app.getDevice(), &framebufferInfo, nullptr, &m_framebuffer));

        // TODO: Assignment 2.7
        // Add the depth image view as a second attachment to the framebuffer.
    }

    void freeFramebuffer() {
        // TODO: Assignment 2.1
        // Free the framebuffer. This function is called when the screen size changes.
        //
        // [ New functions: vkDestroyFramebuffer ]
        vkDestroyFramebuffer(m_app.getDevice(), m_framebuffer, nullptr);
        m_framebuffer = VK_NULL_HANDLE;
    }

    void createVertexBuffers() {
        // TODO: Assignment 2.2
        // Upload these vertices into a vertex buffer.
        // Take a look at the utility function 'uploadBufferWithStaging' and the 'buffer.hpp' file.

        // TODO: Assignment 2.6
        // Upload the vertices of the first mesh in the scene instead.

        // TODO: Assignment 2.9
        // Iterate over the meshes (not nodes!) of the scene and create a vertex buffer for each mesh.

        m_vertexBuffers.reserve(m_scene.meshes.size());

        for (const auto& mesh : m_scene.meshes) {
            m_vertexBuffers.push_back(uploadBufferWithStaging(
                mesh.vertices.data(), mesh.vertices.size() * sizeof(Scene::Vertex), VK_BUFFER_USAGE_VERTEX_BUFFER_BIT
            ));
        }
    }

    void createIndexBuffers() {
        // TODO: Assignment 2.3
        // Create an index buffer that holds six indices pointing to the above vertices. Use a 32-bit unsigned integer
        // for the indices. You should then be able to reduce the number of vertices in the vertex buffer to four.

        m_indexBuffers.reserve(m_scene.meshes.size());

        for (const auto& mesh : m_scene.meshes) {
            m_indexBuffers.push_back(uploadBufferWithStaging(
                mesh.indices.data(), mesh.indices.size() * sizeof(uint32_t), VK_BUFFER_USAGE_INDEX_BUFFER_BIT
            ));
        }

        // TODO: Assignment 2.6
        // Upload the indices of the first mesh in the scene instead.

        // TODO: Assignment 2.9
        // Iterate over the meshes (not nodes!) of the scene and create an index buffer for each mesh.
    }

    void createTextures() {
        const VmaAllocator allocator = m_app.getAllocator();

        m_textureImages.resize(m_scene.textures.size());
        m_textureImageViews.resize(m_scene.textures.size());

        for (int i = 0; i < m_scene.textures.size(); ++i) {
            const auto& texture = m_scene.textures.at(i);

            m_textureImages[i] = uploadImageWithStaging(
                texture.data.data(),
                texture.data.size(),
                VK_IMAGE_TYPE_2D,
                texture.format,
                {texture.width, texture.height, 1},
                VK_IMAGE_USAGE_SAMPLED_BIT, //Shader dar das Bild mit einem Sampler lesen
                VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                texture.mipLevels
            );

            const VkImageViewCreateInfo viewInfo{
                .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
                .image = m_textureImages[i].image,
                .viewType = VK_IMAGE_VIEW_TYPE_2D,
                .format = texture.format,
                .subresourceRange = {
                    .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
                    .baseMipLevel = 0,
                    .levelCount = texture.mipLevels,
                    .baseArrayLayer = 0,
                    .layerCount = 1,
                }
            };

            VK_CHECK(vkCreateImageView(m_app.getDevice(), &viewInfo, nullptr, &m_textureImageViews[i]));
        }
        // TODO: Assignment 2.8
        // Upload the first texture of the scene ('m_scene.textures[0]') to a device local image. The format of
        // the texture is given by 'texture.format'. You may use the utility function 'uploadImageWithStaging'.
        // Then create an image view for the texture image.

        // TODO: Assignment 2.10
        // Now, create an image and view for each texture used in the scene.
    }

    void createTextureSampler() {
        // TODO: Assignment 2.8
        // Create a sampler for textures with linear minification and magnification.

        const VkSamplerCreateInfo samplerInfo{
            .sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO,
            .magFilter = VK_FILTER_LINEAR,
            .minFilter = VK_FILTER_LINEAR,
            .mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR, //Gleichmäßiger übergang zwischen den Mip Leveln
            .addressModeU = VK_SAMPLER_ADDRESS_MODE_REPEAT, // standard 0
            .addressModeV = VK_SAMPLER_ADDRESS_MODE_REPEAT,
            .addressModeW = VK_SAMPLER_ADDRESS_MODE_REPEAT,
            .minLod = 0.0f,
            .maxLod = VK_LOD_CLAMP_NONE //Keine LOD Obergrenze -> GPU kann bis zur kleinsten Mip-Stufe gehen
        };

        VK_CHECK(vkCreateSampler(m_app.getDevice(), &samplerInfo, nullptr, &m_textureSampler));
    }

    void createUniformBuffers() {
        // TODO: Assignment 2.12
        // Upload an instance of 'MaterialUniformBuffer' for each material to the GPU. As the material properties
        // do not change, these buffers can be stored in device local memory. The base color of a material is
        // made up of 'material.colDiffuse' and 'material.opacity'. A material has a texture if
        // 'material.texDiffuseIdx.has_value()' is true.

        m_materialUniformBuffers.reserve(
            m_scene.materials.size()
        ); // Reinen Speicher anlegen {resize würde die Objekte anlegen, dann unten über index zuweisen anstatt
           // push_back}

        for (const auto& material : m_scene.materials) {
            // Übergabe eines materialspezifischen Alpha-Schwellwerts an den Fragmentshader.
            // Für das Laubmaterial „alpha“ der Tokyo-Szene: Verwerfen von Fragmenten mit Alpha < 0.5.
            // Dadurch Vermeidung von Tiefenschreibzugriffen an fast transparenten Blatträndern
            // und Verringerung des dunklen Saums vor den Fenstern. Schwellwert 0.0 für alle
            // übrigen Materialien zur Erhaltung der Transparenzabstufungen bei getöntem Glas.
            //
            // Übliche Vorgehensweise: Kennzeichnung von Laub im glTF-Modell mit alphaMode = MASK
            // und Festlegung des alphaCutoff im Material. Verwerfen der Fragmente unterhalb der
            // Schwelle und deckende Darstellung der übrigen Fragmente. Für Glas Verwendung von
            // alphaMode = BLEND und anschließendes Zeichnen von hinten nach vorne.
            //
            // Auswahl der Schwelle anhand des Materialnamens in diesem Beispiel aufgrund
            // der Kennzeichnung des Laubs als BLEND in der Tokyo-Szene.
            const MaterialUniformBuffer uniformBuffer{
                .baseColor = glm::vec4(material.colDiffuse, material.opacity),
                .hasTexture = material.texDiffuseIdx.has_value() ? 1 : 0,
                .alphacutoff = material.name == "alpha" ? 0.5f : 0.0f,
            };

            m_materialUniformBuffers.push_back(
                uploadBufferWithStaging(&uniformBuffer, sizeof(uniformBuffer), VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT)
            ); // push_back automatisches anhängen an hinterste Stelle
        }
    }

    void createDescriptorPool() {
        // TODO: Assignment 2.8
        // Create a descriptor pool for one combined image sampler.

        const auto materialCount = static_cast<uint32_t>(m_scene.materials.size());

        const VkDescriptorPoolSize poolSizes[] = {
            {.type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, .descriptorCount = materialCount},
            {.type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, .descriptorCount = materialCount}
        };

        const VkDescriptorPoolCreateInfo poolInfo{
            .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,
            .maxSets = materialCount,
            .poolSizeCount = 2,
            .pPoolSizes = poolSizes,
        };

        VK_CHECK(vkCreateDescriptorPool(m_app.getDevice(), &poolInfo, nullptr, &m_descriptorPool));

        // TODO: Assignment 2.10
        // Reserve enough space for as many descriptors and sets as there are materials (not textures!) in the scene.

        // TODO: Assignment 2.12
        // Additionally reserve enough space for one uniform buffer per material.
    }

    void createDescriptorSets() {
        // TODO: Assignment 2.8
        // Allocate a descriptor set and update it by writing the texture image and sampler to binding zero.

        const VkDevice device = m_app.getDevice();

        m_materialDescriptorSets.resize(m_scene.materials.size());

        const VkDescriptorSetAllocateInfo allocInfo{
            .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
            .descriptorPool = m_descriptorPool,
            .descriptorSetCount = 1,
            .pSetLayouts = &m_descriptorSetLayout,
        };

        for (int i = 0; i < m_scene.materials.size(); ++i) {
            const auto& material = m_scene.materials[i];
            const uint32_t textureIdx = material.texDiffuseIdx.value_or(0); // Default value 0
            auto& descriptorSet = m_materialDescriptorSets[i];

            VK_CHECK(vkAllocateDescriptorSets(device, &allocInfo, &descriptorSet));

            const VkDescriptorImageInfo imageInfo{
                .sampler = m_textureSampler,
                .imageView = m_textureImageViews.at(
                    textureIdx
                ), // Hier war kurzzeitig i und nicht textureIdx lolz dann waren die Texturen sehr wild verteilt
                .imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
            };

            const VkDescriptorBufferInfo bufferInfo{
                .buffer = m_materialUniformBuffers[i].buffer, .offset = 0, .range = VK_WHOLE_SIZE
            };

            const VkWriteDescriptorSet writes[]{
                {
                    .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
                    .dstSet = descriptorSet,
                    .dstBinding = 0,
                    .descriptorCount = 1,
                    .descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
                    .pImageInfo = &imageInfo,
                },
                {.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
                 .dstSet = descriptorSet,
                 .dstBinding = 1,
                 .descriptorCount = 1,
                 .descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
                 .pBufferInfo = &bufferInfo}
            };

            vkUpdateDescriptorSets(device, 2, writes, 0, nullptr);
        }

        // TODO: Assignment 2.10
        // Create a seperate descriptor set for each material. The index of the texure a material uses is given
        // by 'material.texDiffuseIdx'. However, the texture index is optional, as some materials do not use a
        // diffuse texture at all. You can use 'optional.has_value()' to check if a material index is defined.
        // There are multiple ways to handle this. You would likely want different pipelines with other shaders
        // for different materials in an actual renderer. Here, you can simply bind the first textures as a
        // fallback if there is no texture defined.

        // TODO: Assignment 2.12
        // Write the uniform buffer in addition to the combined image sampler to the descriptor sets.
    }

    //--------------------------------------------------------------------------------------------------

    void recordCommandBuffer(VkCommandBuffer commandBuffer) {
        const uint32_t currentFrame = m_app.getSwapchain().getCurrentFrameIdx();
        const VkExtent2D extent = m_app.getRenderTargetExtent();

        // TODO: Assignment 2.1
        // First, begin the render pass. Remember to set the render area and the clear value for the color
        // attachment. The subpass contents can be set to 'VK_SUBPASS_CONTENTS_INLINE'. Next, dynamically write
        // the viewport and scissor using the extent from above. Then, bind the graphics pipeline. For now,
        // draw three vertices using 'vkCmdDraw(commandBuffer, 3, 1, 0, 0)'. Their positions are currently
        // hard-coded into the vertex shader. Finally, don't forget to end the render pass.
        //
        // [ New functions: vkCmdBeginRenderPass, vkCmdSetViewport, vkCmdSetScissor, vkCmdBindPipeline,
        //                  vkCmdDraw, vkCmdEndRenderPass ]

        const VkClearValue clearValues[] = {
            {.color = {{0.0f, 0.0f, 0.0f, 1.0f}}}, {.depthStencil = {.depth = 1.0f, .stencil = 0}}
        };

        const VkRenderPassBeginInfo beginInfo{
            .sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO,
            .renderPass = m_renderPass,
            .framebuffer = m_framebuffer,
            .renderArea =
                {
                    .offset = {0, 0},
                    .extent = extent,
                },
            .clearValueCount = 2,
            .pClearValues = clearValues,
        };

        vkCmdBeginRenderPass(
            commandBuffer, &beginInfo, VK_SUBPASS_CONTENTS_INLINE
        ); // VK_SUBPASS_CONTENTS_INLINE: zeichenbefehle werden im selben primären cmdBuffer aufgezeichten

        const VkViewport viewport{
            .x = 0.0f,
            .y = 0.0f,
            .width = static_cast<float>(extent.width),
            .height = static_cast<float>(extent.height),
            .minDepth = 0.0f,
            .maxDepth = 1.0f,
        };

        const VkRect2D scissor{
            .offset = {0, 0},
            .extent = extent,
        };

        vkCmdSetViewport(commandBuffer, 0, 1, &viewport);
        vkCmdSetScissor(commandBuffer, 0, 1, &scissor);
        vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, m_pipeline);

        vk_self_opaqueConditionalSortAndDraw(commandBuffer, m_app.getProjMat(), m_app.getViewMat(), true);
        vk_self_opaqueConditionalSortAndDraw(commandBuffer, m_app.getProjMat(), m_app.getViewMat(), false);

        vkCmdEndRenderPass(commandBuffer);

        // TODO: Assignment 2.2
        // Now, bind the vertex buffer before making the draw call.
        // Then, update the draw call to render six vertices instead of three.
        //
        // [ New functions: vkCmdBindVertexBuffers ]

        // TODO: Assignment 2.3
        // After binding the vertex buffer, also bind the index buffer.
        // Then update the draw call to use the index  buffer.
        //
        // [ New functions: vkCmdBindIndexBuffer, vkCmdDrawIndexed ]

        // TODO: Assignment 2.4
        // Before the draw call, push the push constants for the vertex buffer.
        // You can calculate the MVP matrix by 'm_app.getProjMat() * m_app.getViewMat()'.
        //
        // [ New functions: vkCmdPushConstants ]

        // TODO: Assignment 2.6
        // Update the number of indices to match the mesh and apply the node's model matrix to the MVP matrix.

        // TODO: Assignment 2.7
        // Add a second clear value for the depth image and set it to one for the initial depth.

        // TODO: Assignment 2.8
        // Bind the descriptor set before the draw call.

        // TODO: Assignment 2.9
        // Submit a draw call for each node (not mesh!) of the scene. For each node, create push constants using
        // the transform of the node, and bind the vertex and index buffers for the mesh referenced by the node.
        // Remember that the node and mesh indices are not the same, as multiple nodes can reference the same mesh.
        // Then, submit a draw call for each node, using the correct number of indices.

        // TODO: Assignment 2.10
        // Bind the descriptor set corresponding to the material of the mesh of the current node before submitting
        // the draw calls.

        // TODO: Assignment 2.11
        // When making the draw calls, first draw all the opaque meshes, then all teh transparent meshes. A
        // material stores whether it is transparent in the 'material.alphaMode' (Scene::Material::AlphaMode::Opaque)
        // field. You might want to extract everything corresponding to a draw call into a seperate function.
    }

    void vk_self_opaqueConditionalSortAndDraw(
        VkCommandBuffer commandBuffer, glm::mat4 projection, glm::mat4 view, bool drawOpaque
    ) {
        struct DrawItem {
            uint32_t mesh_index;
            glm::mat4 model;
            float viewZ;
        };

        std::vector<DrawItem> items;

        m_scene.traverseNodes([&](const glm::mat4& model, uint32_t meshIndex) {
            const auto& mesh = m_scene.meshes[meshIndex];
            const bool isOpaque = m_scene.materials[mesh.materialIdx].alphaMode == Scene::Material::AlphaMode::Opaque;

            if (drawOpaque != isOpaque) return;

            float viewZ = 0.0f;
            if (!drawOpaque) {
                const glm::vec3 boundingBoxCenter = (mesh.aabbMin + mesh.aabbMax) * 0.5f;
                const glm::vec4 viewPositionCenter = view * model * glm::vec4(boundingBoxCenter, 1.0f); // hier nur view und nicht projection * view lolz
                viewZ = viewPositionCenter.z;
            }

            items.push_back({meshIndex, model, viewZ});
        });

        if (!drawOpaque) {
            std::stable_sort(items.begin(), items.end(), [](const DrawItem& a, const DrawItem& b) {
                return a.viewZ < b.viewZ;
            }); // stable_sort, damit bei gleichen viewZ die ursprüngliche Reihenfolge beibehalten wird
        }

        for (const auto& item : items) {
            vkCmdBindDescriptorSets(
                commandBuffer,
                VK_PIPELINE_BIND_POINT_GRAPHICS,
                m_pipelineLayout,
                0,
                1,
                &m_materialDescriptorSets[m_scene.meshes[item.mesh_index].materialIdx],
                0,
                nullptr
            );

            const VertexPushConstants pushConstants{.MVP = projection * view * item.model};

            vkCmdPushConstants(
                commandBuffer,
                m_pipelineLayout,
                VK_SHADER_STAGE_VERTEX_BIT,
                0,
                sizeof(VertexPushConstants),
                &pushConstants
            );
            const VkDeviceSize offset = 0;
            vkCmdBindVertexBuffers(commandBuffer, 0, 1, &m_vertexBuffers[item.mesh_index].buffer, &offset);
            vkCmdBindIndexBuffer(commandBuffer, m_indexBuffers[item.mesh_index].buffer, 0, VK_INDEX_TYPE_UINT32);

            vkCmdDrawIndexed(
                commandBuffer, static_cast<uint32_t>(m_scene.meshes[item.mesh_index].indices.size()), 1, 0, 0, 0
            );
        }
    }

    //--------------------------------------------------------------------------------------------------

    void buildUi() {
        if (m_scene.nodes.size() > 0) {
            ImGui::Text("Scene '%s'", m_scene.name.c_str());
            ImGui::Separator();
            ImGui::Text("Nodes     : %d", static_cast<uint32_t>(m_scene.nodes.size()));
            ImGui::Text("Meshes    : %d", static_cast<uint32_t>(m_scene.meshes.size()));
            ImGui::Text("Materials : %d", static_cast<uint32_t>(m_scene.materials.size()));
            ImGui::Text("Textures  : %d", static_cast<uint32_t>(m_scene.textures.size()));
        }
    }

    //--------------------------------------------------------------------------------------------------

    Buffer uploadBufferWithStaging(const void* data, VkDeviceSize size, VkBufferUsageFlags usage) {
        const VmaAllocator allocator = m_app.getAllocator();

        Buffer stagingBuffer{};
        VK_CHECK(stagingBuffer.init(
            allocator,
            size,
            VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT
        ));

        void* mapped;
        VK_CHECK(vmaMapMemory(allocator, stagingBuffer.allocation, &mapped));
        std::memcpy(mapped, data, size);
        vmaUnmapMemory(allocator, stagingBuffer.allocation);

        Buffer resultBuffer{};
        VK_CHECK(resultBuffer.init(
            allocator,
            size,
            usage | VK_BUFFER_USAGE_TRANSFER_DST_BIT, // required for copy
            VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT
        ));

        VK_CHECK(m_app.executeSingleTimeCommand([&](VkCommandBuffer commandBuffer) {
            const VkBufferCopy copyRegion{.size = size};
            vkCmdCopyBuffer(commandBuffer, stagingBuffer.buffer, resultBuffer.buffer, 1, &copyRegion);
        }));

        stagingBuffer.deinit(allocator);

        return resultBuffer;
    }

    Image uploadImageWithStaging(
        const void* data,
        VkDeviceSize size,
        VkImageType imageType,
        VkFormat format,
        VkExtent3D extent,
        VkImageUsageFlags usage,
        VkImageLayout finalLayout,
        uint32_t mipLevels = 1
    ) {
        const VmaAllocator allocator = m_app.getAllocator();

        Buffer stagingBuffer{};
        stagingBuffer.init(
            allocator,
            size,
            VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT
        );

        void* mapped;
        VK_CHECK(vmaMapMemory(allocator, stagingBuffer.allocation, &mapped));
        std::memcpy(mapped, data, size);
        vmaUnmapMemory(allocator, stagingBuffer.allocation);

        Image resultImage{};
        resultImage.init(
            allocator,
            imageType,
            format,
            extent,
            VK_SAMPLE_COUNT_1_BIT,
            VK_IMAGE_TILING_OPTIMAL,
            usage | VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT,
            VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
            mipLevels
        );

        VK_CHECK(m_app.executeSingleTimeCommand([&](VkCommandBuffer commandBuffer) {
            const auto transferLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
            resultImage.transitionLayout(commandBuffer, format, VK_IMAGE_LAYOUT_UNDEFINED, transferLayout, mipLevels);

            const VkBufferImageCopy region{
                .bufferOffset = 0,
                .bufferRowLength = 0,
                .bufferImageHeight = 0,
                .imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1},
                .imageOffset = {0, 0, 0},
                .imageExtent = extent,
            };
            vkCmdCopyBufferToImage(commandBuffer, stagingBuffer.buffer, resultImage.image, transferLayout, 1, &region);

            const auto srcLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
            auto width = static_cast<int32_t>(extent.width);
            auto height = static_cast<int32_t>(extent.height);

            for (uint32_t level = 1; level < mipLevels; ++level) {
                const uint32_t previous = level - 1;
                const int32_t nextWidth = std::max(1, width / 2);
                const int32_t nextHeight = std::max(1, height / 2);

                resultImage.transitionLayout(commandBuffer, format, transferLayout, srcLayout, 1, previous);

                const VkImageBlit blit{
                    .srcSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, previous, 0, 1},
                    .srcOffsets = {{0, 0, 0}, {width, height, 1}},
                    .dstSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, level, 0, 1},
                    .dstOffsets = {{0, 0, 0}, {nextWidth, nextHeight, 1}},
                };

                vkCmdBlitImage(
                    commandBuffer,
                    resultImage.image,
                    srcLayout,
                    resultImage.image,
                    transferLayout,
                    1,
                    &blit,
                    VK_FILTER_LINEAR
                );

                resultImage.transitionLayout(commandBuffer, format, srcLayout, finalLayout, 1, previous);

                width = nextWidth;
                height = nextHeight;
            }

            resultImage.transitionLayout(commandBuffer, format, transferLayout, finalLayout, 1, mipLevels - 1);
        }));

        stagingBuffer.deinit(allocator);

        return resultImage;
    }
};

//--------------------------------------------------------------------------------------------------

int main() {
    Assignment app;
    app.init();
    app.run();
    app.deinit();

    return EXIT_SUCCESS;
}
