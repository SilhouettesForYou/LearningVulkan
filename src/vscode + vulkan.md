# vscode + vulkan

## vscode 配置C/C++环境

### 1. 安装cpptools插件

### 2. 安装MinGW并配置环境变量

### 3. vscode配置文件

#### 配置文件目录结构如下

> 扩展：VS Code supports variable substitution inside strings in `launch.json` and has the following predefined variables:
>
> * `${workspaceFolder}` - the path of the folder opened in VS Code
> * `${workspaceRootFolderName} ` - the name of the folder opened in VS Code without any slashes (/)
> * `${file}` - the current opened file
> * `${relativeFile}` - the current opened file relative to `${workspaceRoot}`
> * `${fileBasename}` - the current opened file's basename
> * `${fileBasenameNoExtension}` - the current opened file's basename with no file extension
> * `${fileDirname}` - the current opened file's dirname
> * `${fileExtname}` - the current opened file's extension
> * `${cwd}` - the task runner's current working directory on startup
> * `${lineNumber}` - the current selected line number in the active file

![image-20200408153825611](./images/image-20200408153825611.png)

- `launch.json`

```json
{
    // Use IntelliSense to learn about possible attributes.
    // Hover to view descriptions of existing attributes.
    // For more information, visit: https://go.microsoft.com/fwlink/?linkid=830387
    "version": "0.2.0",
    "configurations": [
        {
            "name": "g++.exe - 生成和调试活动文件", // 配置名称，将会在启动配置的下拉菜单中显示
            "type": "cppdbg",	// 配置类型，这里只能为cppdbg
            "request": "launch",	// 请求配置类型，可以为launch（启动）或attach（附加）
            "program": "${workspaceFolder}/${fileBasenameNoExtension}.exe",// 将要进行调试的程序的路径 
            "args": [],	// 程序调试时传递给程序的命令行参数，一般设为空即可
            "stopAtEntry": false,	// 设为true时程序将暂停在程序入口处，一般设置为false
            "cwd": "${workspaceFolder}", // 调试程序时的工作目录，一般为${workspaceFolder}即代码所在目录
            "environment": [],
            "externalConsole": true,	// 调试时是否显示控制台窗口，一般设置为true显示控制台
            "MIMode": "gdb",
            "miDebuggerPath": "D:\\MinGW\\bin\\gdb.exe", //miDebugger的路径，注意这里要与MinGw的路径对应
            "setupCommands": [
                {
                    "description": "为 gdb 启用整齐打印",
                    "text": "-enable-pretty-printing",
                    "ignoreFailures": true
                }
            ],
            "preLaunchTask": "g++.exe build active file" // 调试会话开始前执行的任务，一般为编译程序，c++为g++, c为gcc
        }
    ]
}
```

-  `tasks.json` 

```json
{
    "version": "2.0.0",
    "tasks": [
        {
            "type": "shell",
            "label": "g++.exe build active file", 
            "command": "D:\\MinGW\\bin\\g++.exe",
            "args": [
                "-g",
                "${file}",
                "-o",
                "${fileDirname}\\${fileBasenameNoExtension}.exe",
                "-std=c++17",
                "-lglfw3",
                "-lvulkan-1"
            ],
            "options": {
                "cwd": "D:\\MinGW\\bin"
            },
            "problemMatcher": [
                "$gcc"
            ]
        }
    ]
}
```

- `c_cpp_properties.json`

```json
{
    "configurations": [
        {
            "name": "Win32",
            "includePath": [
                "D:\\MinGW\\include\\",
                "D:\\MinGW\\lib\\gcc\\mingw32\\8.2.0\\include",
                "${workspaceRoot}"
            ],
            "defines": [
                "_DEBUG",
                "UNICODE"
            ],
            "intelliSenseMode": "msvc-x64",
            "browse": {
                "path": [
                    "D:\\MinGW\\include\\",
                    "D:\\MinGW\\lib\\gcc\\mingw32\\8.2.0\\include",
                    "${workspaceRoot}"
                ],
                "limitSymbolsToIncludedHeaders": true,
                "databaseFilename": ""
            },
            "compilerPath": "C:/Program Files (x86)/Microsoft Visual Studio/2019/Community/VC/Tools/MSVC/14.23.28105/bin/Hostx64/x64/cl.exe",
            "cStandard": "c11",
            "cppStandard": "c++17"
        }
    ],
    "version": 4
}
```

```json
{
    "configurations": [
        {
            "name": "MinGW",
            "intelliSenseMode": "clang-x64",
            "compilerPath": "C:/Program Files/LLVM/bin/gcc.exe",
            "includePath": [
                "${workspaceFolder}"
            ],
            "defines": [],
            "browse": {
                "path": [
                    "${workspaceFolder}"
                ],
                "limitSymbolsToIncludedHeaders": true,
                "databaseFilename": ""
            },
            "cStandard": "c11",
            "cppStandard": "c++17"
        }
    ],
    "version": 4
}
```

### 4. 相关库

#### [GLFW](https://www.glfw.org/download.html) 窗口库

![image-20200409100444094](./images/1571382183481.png)

**一般MinGW默认下载为32位，所以GLFW选择32位的下载**

### [GLM]( https://github.com/g-truc/glm) 线性代数计算库

### 5. 头文件

![image-20200409101112783](./images/image-20200409101112783.png)

* 将`VulkanSDK`目录下的`/Include/vulkan`复制到上图路径下
* 将`glfw-x.x.x.bin.WIN32`目录下的`/include/GLFW`复制到上图路径下
* 将`glm`目录下的`/glm`复制到上图路径下

### 6. 库文件

![image-20200409102255333](./images/image-20200409102255333.png)

![image-20200409102839260](D:\Notebook\images\image-20200409102738233.png)

将上述框内的`.lib`复制到`~/MinGW/mingw32/lib`目录下

### 7. 编译参数

在`tasks.json`文件中的`args`添加两个编译参数`-lglfw3`、`-lvulkan-1`，同时添加`-std=c++17`

```json
"args": [
                "-g",
                "${file}",
                "-o",
                "${fileDirname}\\${fileBasenameNoExtension}.exe",
                "-std=c++17",
                "-lglfw3",
                "-lvulkan-1"
         ]
```



