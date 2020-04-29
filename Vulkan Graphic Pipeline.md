```mermaid
graph TB
    subgraph 帧缓冲区
        Frame1("深度-模板附件")
        Frame2("输入附件")
        Frame3("颜色附件")
    end
    
    subgraph 描述符
        Desc1("推送常量")
        Desc2("图像")
        Desc3("统一和存储缓冲")
        Desc4("纹理缓冲区")
    end
    
    subgraph 缓冲区
        A1("间接缓冲区")
        B1("索引缓冲区")
        B2("顶点缓冲区")
    end
    
	subgraph main
		Start(("开始"))-->A
		A("绘制")-->B("输入装配")
        B("输入装配")-->C("顶点着色器")
        C("顶点着色器")-->D("细分控制着色器")
        D("细分控制着色器")-->E("细分评估着色器")
        E("细分评估着色器")-->F("几何着色器")
        F("几何着色器")-->G("图元组装")
        G("图元组装")-->H("裁剪和删除")
        H("裁剪和删除")-->I("光栅器")
        I("光栅器")-->J("前置片段操作")
        J("前置片段操作")-->K("片段着色器")
        K("片段着色器")-->L("后置片段操作")
        L("后置片段操作")-->M("颜色混合")
        
  		Joint1(("+"))
  		Joint2(("+"))
  		
  		M-->End(("结束"))
	end
  	A1-.->A
  	B1-.->B
  	B2-.->B
  
  	C-.-Joint1
  	D-.-Joint1
  	E-.-Joint1
  	F-.-Joint1
  	K-.-Joint1
  	Desc1-.-Joint1
  	Desc2-.-Joint1
  	Desc3-.-Joint1
  	Desc4-.-Joint1
  
  	J-.-Joint2
  	K-.-Joint2
  	L-.-Joint2
  	M-.-Joint2
  	Frame1-.-Joint2
  	Frame2-.-Joint2
  	Frame3-.-Joint2
  	style A fill:#89ba16,stroke:#000,stroke-width:2px
  	style A1 fill:#89ba16,stroke:#000,stroke-width:2px
  	style B fill:#8db9ca,stroke:#000,stroke-width:2px  	
  	style B1 fill:#8db9ca,stroke:#000,stroke-width:2px  	  	
  	style B2 fill:#8db9ca,stroke:#000,stroke-width:2px
    style C fill:#efdf00,stroke:#000,stroke-width:2px
    style D fill:#efdf00,stroke:#000,stroke-width:2px
    style E fill:#efdf00,stroke:#000,stroke-width:2px
    style F fill:#efdf00,stroke:#000,stroke-width:2px
    style K fill:#efdf00,stroke:#f66,stroke-width:2px,stroke-dasharray: 5, 5
    style Joint1 fill:#efdf00,stroke:#000,stroke-width:2px
    style Desc1 fill:#efdf00,stroke:#000,stroke-width:2px
    style Desc2 fill:#efdf00,stroke:#000,stroke-width:2px
    style Desc3 fill:#efdf00,stroke:#000,stroke-width:2px
    style Desc4 fill:#efdf00,stroke:#000,stroke-width:2px
    style J fill:#48a9c5,stroke:#f66,stroke-width:2px,stroke-dasharray: 5, 5
    style L fill:#48a9c5,stroke:#f66,stroke-width:2px,stroke-dasharray: 5, 5
    style M fill:#48a9c5,stroke:#f66,stroke-width:2px,stroke-dasharray: 5, 5
    style Joint2 fill:#48a9c5,stroke:#f66,stroke-width:2px,stroke-dasharray: 5, 5
    style Frame1 fill:#48a9c5,stroke:#f66,stroke-width:2px,stroke-dasharray: 5, 5
    style Frame2 fill:#48a9c5,stroke:#f66,stroke-width:2px,stroke-dasharray: 5, 5
    style Frame3 fill:#48a9c5,stroke:#f66,stroke-width:2px,stroke-dasharray: 5, 5
    
  	style G fill:#74d2e7,stroke:#000,stroke-width:2px
  	style H fill:#74d2e7,stroke:#000,stroke-width:2px
  	style I fill:#74d2e7,stroke:#000,stroke-width:2px
  	
	style Start fill:#009f4d,stroke:#000,stroke-width:2px
	style End fill:#f66,stroke:#000,stroke-width:2px
```

* 绘制：命令进入`Vulkan`图像管线的地方。通常，`Vulkan`设备里一个很小的额处理器或者专用的硬件对命令缓冲区的命令进行解释，并直接和硬件交互来触发工作。
* 输入装配：该阶段读取索引缓存区和顶点缓存区，它们包含了顶点信息，这些顶点组成了你想要绘制的图形。
* 顶点着色器：这是顶点着色器执行的地方。它以属性顶点作为输入，并为下一个阶段准备交换的和处理的顶点数据。
* 细分控制着色器：这个可编程的着色器阶段负责生产细分因子和其他逐片元（patch）数据（被固定功能细分引擎使用）。
* *细分图元生成：这个固有功能阶段使用在细分控制着色器产生的细分因子，来吧图片元分解成许多更小的、更简单的图元，以供细分评估着色器使用。*
* 细分评估着色器：这个着色阶段运行在细分图元生成器中产生的每一个顶点上。它和顶点着色器操作类似——除了输入顶点是生成的以外，而非从内存读取的。
* 几何着色器：这个着色阶段在整个图元上运行。图元可能是点、直线或者三角形，或它们的变种（在它们周围有额外顶点）。这个阶段也有在管线中间改变图元类型能力。
* 图元组装：这个阶段把顶点、细分或几何阶段产生的顶点分组，将它们分组成适合光栅化的图元。它也剔除或裁剪图元，并把图元变换进合适的视口。
* 裁剪和剔除：这个固定功能阶段决定了图元的哪一部分可能成为输出图像的一部分，并抛弃那些不会组成图像的部分，把可见的图元发给光栅器。
* 光栅器：光栅器是`Vulkan`中所有的图形的基本核心。光栅器接受装配好的图元（仍然用一系列顶点表示），并把它们变成单独的片段，片段可能会变成图像的像素。
* 前置片段操作：在着色之前一旦知道了片段的位置，就可以在片段上进行好几个操作。这些前置片段操作包括深度和模板测试（当开启了两个测试时）。
* *片段装配：片段装配阶段接受光栅器的输出，以及任何逐片段数据，将这些信息作为一组，发给片段着色阶段。*
* 片段着色器：这个阶段运行管线里最后着色器。负责计算发送到最后固有功能处理阶段的数据。
* 后置片段操作：在某些情况下，片段着色器会修改本应该在前置片段操作中使用的数据。在这种情况下，这些前置片段操作转移到后置片段操作中执行。
* 颜色混合：颜色操作接受前置片段着色器和后置片段着色器操作的结果，并使用它们更新帧缓冲区。颜色操作包括混合与逻辑操作。