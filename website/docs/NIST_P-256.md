---
title:NIST_P-256
---

NIST P-256（也称为 secp256r1 或 prime256v1）是美国国家标准与技术研究院（NIST）定义的一条标准化椭圆曲线，属于椭圆曲线密码学（ECC）体系中的核心组件之一。它被广泛用于现代安全协议中，如 TLS、SSH、IPsec 以及蓝牙安全连接等。
<img src="/images/file_0000000013647208a9fa234b4c5445c6.png" alt="1">
NIST P-256的签名流程其实可以简要的分为图片上的4个流程，每个流程都可以粗略总结为相应的数学公式
要实现该曲线的签名流程就必须先理解其背后的数学逻辑

## 数学理解

### 1.点乘 $R=kG$

k为随机数，G为曲线$y^2 = x^3 + ax + b$上的一个点坐标，$kG$在数学上可以理解为\
 $kG = \underbrace{G + G + \cdots + G}_{k\ \text{次}}$ \
也就是说，把点 G 沿着曲线“走” k 次，就能得到点$R(x_R,y_R)$，实际计算中使用倍点与加点算法，其数学定义等价于$k$次点加。

### 2.取数 $r = x_R\bmod n$

该公式的计算结果最终为$r \in [0, n-1]$,点$R$是二维坐标，我们所需要的签名值为数字，故取$x_R$进行取模运算以得到签名值$(r,s)$之中的横坐标，特别需要注意的是如果最后计算得到的$r=0$那么我们需要更换$k$值进行重新计算

### 3.计算$k^{-1}$

$k^{-1}$并不是简简单单的k的倒数，而是模$n$意义下的乘法逆元，而非实数域中的倒数，其满足公式$k^{-1}\equiv k^{n-2}(mod n)$，所有标准安全曲线（如 secp256k1、NIST P-256 等）都要求 n 是一个素数，故根据费马小定理，该公式完全成立

### 4.计算s坐标

$(r,s)$中的$r$在前面的计算中已经得到，在这里我们根据$s=k^{-1}(z+rd)modn$计算$s$的值，若计算得到 $s=0$，同样需要重新选择 $k$,最终，我们可以输出签名结果$(r,s)$。其中 $z$是待签名消息的哈希值（通常使用 SHA-256）截断或转换为整数后的结果，且 $z\in [0, n-1]$。

## 编写代码
前面我们已经理解了NIST P-256的数学逻辑，在上述过程中的一些量NIST已经给出，他们分别为(b值签名流程没有用到故未写出)
|名称|值|
|----|----|
|p|$p = 2^{256} - 2^{224} + 2^{192} + 2^{96} - 1$|
|a|$a=p-3$|
|$G_x$|$6B17D1F2E12C4247F8BCE6E563A440F277037D812DEB33A0F4A13945D898C296$|
|$G_y$|$4FE342E2FE1A7F9B8EE7EB4A7C0F9E162BCE33576B315ECECBB6406837BF51F5$|
|基点阶$n$|$FFFFFFFF00000000FFFFFFFFFFFFFFFFBCE6FAADA7179E84F3B9CAC2FC632551$|

### 1.主要逻辑
根据上面的表格，我们先声明变量
```python
p = 0xFFFFFFFF00000001000000000000000000000000FFFFFFFFFFFFFFFFFFFFFFFF
a = p - 3
Gx = 0x6B17D1F2E12C4247F8BCE6E563A440F277037D812DEB33A0F4A13945D898C296
Gy = 0x4FE342E2FE1A7F9B8EE7EB4A7C0F9E162BCE33576B315ECECBB6406837BF51F5
n  = 0xFFFFFFFF00000000FFFFFFFFFFFFFFFFBCE6FAADA7179E84F3B9CAC2FC632551
```
C语言对于大数运算的支持几乎没有，故我们使用Python进行演示，按照上部分的数学逻辑编写签名函数
```python
def ecdsa_sign(priv, msg_hash):
    while True:
        k = 0x123456 #演示值
        R = point_mul(k, ECPoint(Gx, Gy))
        r = R.x % n
        if r == 0:
            continue
        k_inv = mod_inv(k, n)
        s = (k_inv * (msg_hash + r * priv)) % n
        if s == 0:
            continue
        return (r, s)
```

### 2.私钥安全
在实际环境中，k值理应不可预测且不可重复，如果你使用相同的k值对不同消息$z_1,z_2$进行签名:  

$s_1 =k^{-1}(z_1+rd)modn$  
$s_2 = k^{-1}(z_2+rd)modn$  

由于 r 相同（因为 k 相同导致 R 点相同），我们可以对两条签名做减法：

$s_1 - s_2 \equiv k^{-1} (z_1 + r d - (z_2 + r d)) \equiv k^{-1} (z_1 - z_2) \pmod n$

从而可以解出 k：

$k \equiv \frac{z_1 - z_2}{s_1 - s_2} \pmod n$

一旦 k 被求出，就可以直接恢复私钥 d：

$d \equiv \frac{s_1 k - z_1}{r} \pmod n$ 

故此，重复或可预测的 k 必然会导致私钥泄露，因此在生产环境中必须使用 安全的随机 k 或 RFC 6979 确定性生成 k。

### 3.辅助函数与变量
下面给出 Python 示例中用于椭圆曲线运算的辅助类和函数：
```python
class ECPoint:
    def __init__(self, x, y, infinity=False):
        self.x = x
        self.y = y
        self.infinity = infinity

def mod_inv(k, p):
    """计算 k 在 p 下的模逆元"""
    return pow(k, p-2, p)  # 费马小定理

def point_add(P, Q):
    """椭圆曲线加法"""
    if P.infinity:
        return Q
    if Q.infinity:
        return P
    if P.x == Q.x and P.y != Q.y:
        return ECPoint(0,0,True)  # 无穷远点

    if P.x == Q.x:
        # P = Q, 使用切线公式
        l = (3 * P.x * P.x + a) * mod_inv(2 * P.y, p) % p
    else:
        # P != Q
        l = (Q.y - P.y) * mod_inv(Q.x - P.x, p) % p

    x_r = (l*l - P.x - Q.x) % p
    y_r = (l*(P.x - x_r) - P.y) % p
    return ECPoint(x_r, y_r)

def point_mul(k, P):
    """标量乘法，双倍-加法算法"""
    R = ECPoint(0,0,True)  # 无穷远点
    addend = P

    while k:
        if k & 1:
            R = point_add(R, addend)
        addend = point_add(addend, addend)
        k >>= 1
    return R
```
### 4.测试
//TODO: