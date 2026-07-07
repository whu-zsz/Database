## RMDB使⽤指南

```txt
RMDB使用指南
环境配置
项目下载
编译
运行 (S/C)
单元测试
flex & bison文件的修改
```

## 环境配置

RMDB需要以下依赖环境库配置：

gcc 7.1及以上版本（要求完全⽀持C++17）

cmake 3.16及以上版本

flex 

bison 

readline 

可以通过命令完成环境配置(以Debian/Ubuntu-apt为例)

```shell
sudo apt-get install build-essential # build-essential packages, including gcc, g++, make and so on
sudo apt-get install cmake # cmake package
sudo apt-get install flex bison # flex & bison packages
sudo apt-get install libreadline-dev # readline package 
```

可以通过 cmake --version 命令来查看cmake版本，如果低于3.16，需要在官⽹下载3.16以上的版本并解压，⼿动进⾏安装。

注意,在CentOS下,编译时可能存在头⽂件冲突的问题,我们不建议你使⽤Ubuntu以外的操作系统。

## 项⽬下载

RMDB位于DB2023仓库中。

## 编译

整个系统分为服务端和客户端，你可以使⽤以下命令来进⾏服务端的编译：

```txt
mkdir build
cd build
cmake .. [-DCMAKE_BUILD_TYPE=Debug] | [-DCMAKE_BUILD_TYPE=Release] # [ ]中为可选项
make rmdb <-j4> | <-j8> # 在未完成代码之前，无法编译成功
```

可以使⽤以下命令来进⾏客户端的编译：

```batch
cd rmdb_client
mkdir build
cd build
cmake .. [-DCMAKE_BUILD_TYPE=Debug] | [-DCMAKE_BUILD_TYPE=Release]
make rmdb_client <-j4>| <-j8> # 选择4 or 8线程编译
```

## 运⾏ (S/C)

⾸先运⾏服务端：

```txt
cd build
./bin/rmdb <database_name> # 如果存在该数据库, 直接加载; 若不存在该数据库, 自动创建
```

然后开启客户端，⽤户可以同时开启多个客户端：

```txt
cd rmdb_client/build
./rmdb_client 
```

⽤户可以通过在客户端界⾯使⽤exit命令来进⾏客户端的关闭：

```txt
RMDB> exit; 
```

服务端的关闭需要在服务端运⾏界⾯使⽤ctrl+c来进⾏关闭，关闭服务端时，系统会把数据⻚刷新到磁盘中。

如果需要删除数据库，则需要在build⽂件夹下删除与数据库同名的⽬录

如果需要删除某个数据库中的表⽂件，则需要在build⽂件夹下找到数据库同名⽬录，进⼊该⽬录，然后删除表⽂件

## 单元测试

单元测试使⽤GoogleTest框架，在项⽬的src/⽂件夹下包含测试示例⽂件unit_test.cpp⽂件，参赛队伍可以运⾏unit_test单元测试来了解单元测试流程。

以unit_test为例，可以通过以下命令进⾏测试：

```shell
cd build
make unit_test
./bin/unit_test 
```

## flex & bison⽂件的修改

在parser⼦⽂件夹下涉及flex和bison⽂件的修改，开发者修改lex.l和yacc.y⽂件之后，需要通过以下命令重新⽣成对应⽂件：

```batch
flex --header-file=lex.yy.hpp -o lex.yy.cpp lex.l
bison --defines=yacc.tab.hpp -o yacc.tab.cpp yacc.y 
```