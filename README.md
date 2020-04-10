# AGV_IMAGE
The image processing code for AGV to find the point to follow
target:
- find the red line in the 240X320 images
- fit the lines
- return the point in the 90th row! 

[自然场景图像中的文本检测综述]:http://www.aas.net.cn/CN/abstract/abstract19393.shtml
[文本检测综述]:https://blog.csdn.net/m0_38007695/article/details/100133117
[机器学习算法指标评价]:https://www.cnblogs.com/Zhi-Z/p/8728168.html

> LATEX:
>
> 上标 ：`x^{a+b+c}` : $x^{a+b+c}$
>
> 下标 ： `x_{a+b+c}` : $x_{a+b+c}$
>
> 

卷积：

input:$X\in R^{M*N*D}$

Kernel:$W\in R^{W*H*D*P}$

output:$Y\in R^{M^`*N^`*P}$

> 为了计算输出特征映射$Y^p$ ，用卷积核$W^{p_1}$ ,$W^{p_2}$,··· ,$W^{p_D}$ 分别对输入特
> 征映射$X^1$ ,$X^2$ ,··· ,$X^D$进行卷积，然后将卷积结果相加，并加上一个标量偏置b
> 得到卷积层的净输入$Z^p$。 

[VGG16详解]:https://www.cnblogs.com/lfri/p/10493408.html

Anchors

RPN网络

IOU重叠度

反卷积

UpConv
