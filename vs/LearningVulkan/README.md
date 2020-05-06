# Learning Vulkan

### 项目结构

![project framework](../../img/project-framework.png)

> 自上至下的区域分别是

* Vulkan基本框架
* Windows界面
  * [如何评价imgui](https://www.zhihu.com/question/267602287)
  * [imgui入门笔记](https://www.dazhuanlan.com/2019/12/16/5df6a0950ca0c/)
* Example示例项目

### 项目的属性配置

#### imgui

![imgui property](../../img/imgui-property.png)

```mermaid
graph LR
	A("项目属性")
	B("常规")
	C("配置类型")
	D("静态库(.lib)")
	
	A --> B --> C -.-> D
```

#### base

![base property step 1](./../../img/base-property-step-1.png)

```mermaid
graph LR
	A("项目属性")
	B("常规")
	
	C("输出目录")
	D("$(SolutionDir)\Lib\")
	E("配置类型")
	F("静态库(.lib)")
	G("C++语言标准")
	H("C++17")
	
	A --> B --> C -."可选".-> D
	B --> E -.-> F
	B --> G -.-> H
```

![base property step 2](D:\Code\Vulkan-code\LearningVulkan\img\base-property-step-2.png)

```mermaid
graph LR
	A("项目属性")
	B("C/C++")
	C("常规")
	D("附加包含目录")
	E("D:\VulkanSDK\1.2.131.2\Include")
	F("D:\VulkanSDK\1.2.131.2\Third-Party\glfw-3.3.2.bin.WIN32\include")
	G("D:\VulkanSDK\1.2.131.2\Third-Party\glm")
	H("../imgui")
	
	A --> B --> C --> D
	D -.-> E
    D -.-> F
	D -.-> G
    D -.-> H
```

#### Example

![example property step 1](../../img/example-property-step-1.png)

```mermaid
graph LR
	A("项目属性")
	B("常规")
	
	C("配置类型")
	D("应用程序(.exe)")
	E("C++语言标准")
	F("C++17")
	
	A --> B --> C -."默认".-> D
	B --> E -.-> F

```
---

![example property step 2](../../img/example-property-step-2.png)

```mermaid
graph LR
	A("项目属性")
	B("高级")
	C("字符集")
	D("使用多字节字符集")
	
	A --> B --> C -.-> D
```
---

![example property step 3](../../img/example-property-step-3.png)

```mermaid
graph LR
	A("项目属性")
	B("C/C++")
	C("常规")
	D("附加包含目录")
	E("D:\VulkanSDK\1.2.131.2\Include")
	F("D:\VulkanSDK\1.2.131.2\Third-Party\glfw-3.3.2.bin.WIN32\include")
	G("D:\VulkanSDK\1.2.131.2\Third-Party\glm")
	H("../imgui")
	I("../base")
	
	A --> B --> C --> D
	D -.-> E
    D -.-> F
	D -.-> G
    D -.-> H
    D -.-> I
```
---

![example property step 4](./../../img/example-property-step-4.png)

```mermaid
graph LR
	A("项目属性")
	B("连接器")
	C("常规")
	D("附加包含目录")
	E("D:\VulkanSDK\1.2.131.2\Lib32")
	F("D:\VulkanSDK\1.2.131.2\Third-Party\glfw-3.3.2.bin.WIN32\lib-vc2019")
	G("../Lib")
	
	A --> B --> C --> D
	D -.-> E
    D -.-> F
	D -.-> G
```
---

![example property step 5](D:\Code\Vulkan-code\LearningVulkan\img\example-property-step-5.png)

```mermaid
graph LR
	A("项目属性")
	B("连接器")
	C("输入")
	D("附依赖项")
	E("vulkan-1.lib")
	F("glfw3.lib")
	G("base.lib")
	H("imgui.lib")
	
	A --> B --> C --> D
	D -.-> E
    D -.-> F
	D -.-> G
	D -.-> H
```

### ktx的手工配置

* 项目**ktx**的目录结构，主要将关键的`.h`和`.c`文件添加到该项目中

![config ktx 1](../../img/config-ktx-1.png)

* 配置**ktx**属性

![config ktx 2](../../img/config-ktx-2.png)

![config ktx 3](../../img/config-ktx-3.png)

