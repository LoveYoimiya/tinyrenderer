#include "our_gl.h"

// Model矩阵负责将物体从其本地坐标系转换到世界坐标系
// View矩阵负责将世界坐标系的物体转换到相机坐标系
mat<4, 4> ModelView;
// 将3D视点坐标系下的物体转换到裁剪坐标系，实现近大远小的透视效果
mat<4, 4> Projection;
// 将裁剪坐标系下的物体转换到屏幕坐标系，[-1, 1], [-1, 1] -> [0, width], [0, height]
mat<4, 4> Viewport;

/**
 * @brief 获取Modelview矩阵
 * 
 * @param eye 相机坐标
 * @param center 相机坐标系原点
 * @param up 相机的朝上方向
 */
void lookat(const vec3 eye, const vec3 center, const vec3 up) { // check https://github.com/ssloy/tinyrenderer/wiki/Lesson-5-Moving-the-camera
    vec3 z = (center-eye).normalized();
    vec3 x =  cross(up,z).normalized();
    vec3 y =  cross(z, x).normalized();
    mat<4,4> Minv = {{{x.x,x.y,x.z,0},   {y.x,y.y,y.z,0},   {z.x,z.y,z.z,0},   {0,0,0,1}}};
    mat<4,4> Tr   = {{{1,0,0,-eye.x}, {0,1,0,-eye.y}, {0,0,1,-eye.z}, {0,0,0,1}}};
    ModelView = Minv*Tr;
}

/**
 * @brief 获取Projection矩阵
 * 
 * @param f -1/f决定了深度值对于透视投影的影响， 值越大越远离观察点的物体在投影后看起来越小
 */
void projection(const double f) { // check https://en.wikipedia.org/wiki/Camera_matrix
    Projection = {{{1,0,0,0}, {0,-1,0,0}, {0,0,1,0}, {0,0,-1/f,0}}};
}

/**
 * @brief 获取Viewport矩阵
 * 
 * @param x 视口在屏幕坐标系的x坐标
 * @param y 视口在屏幕坐标系的y坐标
 * @param w 视口宽
 * @param h 视口高
 */
// —————————————————
// |               |
// |   ————w————   |
// |   |       |   |
// |   |h      |   |
// |   |       |   |
// |(x,y)———————   |
// —————————————————

void viewport(const int x, const int y, const int w, const int h) {
    Viewport = {{{w/2., 0, 0, x+w/2.}, {0, h/2., 0, y+h/2.}, {0,0,1,0}, {0,0,0,1}}};
}

/**
 * @brief 计算某个点(P)在对应三角形(A, B, C)的重心坐标(alpha, beta, gamma)
 */
vec3 barycentric(const vec2 tri[3], const vec2 P) {
    mat<3,3> ABC = {{embed<3>(tri[0]), embed<3>(tri[1]), embed<3>(tri[2])}};
    if (ABC.det()<1e-3) return {-1,1,1}; // for a degenerate triangle generate negative coordinates, it will be thrown away by the rasterizator
    return ABC.invert_transpose() * embed<3>(P);
}

/**
 * @brief 绘制三角形
 * 
 * @param clip_verts 三角形的三个顶点坐标
 * @param shader 着色器
 * @param image 屏幕：宽w, 高h
 * @param zbuffer 深度缓存
 */
void triangle(const vec4 clip_verts[3], IShader &shader, TGAImage &image, std::vector<double> &zbuffer) {
    // 进行视口变换
    vec4 pts[3]  = { Viewport*clip_verts[0],    Viewport*clip_verts[1],    Viewport*clip_verts[2]    };  // triangle screen coordinates before persp. division
    vec2 pts2[3] = { proj<2>(pts[0]/pts[0][3]), proj<2>(pts[1]/pts[1][3]), proj<2>(pts[2]/pts[2][3]) };  // triangle screen coordinates after  perps. division
    // 像素坐标范围：x~(0, w - 1), y~(0, h - 1)
    // 初始化包围盒，bboxmin为屏幕宽高，bboxmax为0
    int bboxmin[2] = {image.width()-1, image.height()-1};
    int bboxmax[2] = {0, 0};
    // 创造三角形的包围盒,bboxmin是左下角坐标，bboxmax是右上角坐标
    for (int i=0; i<3; i++)
        for (int j=0; j<2; j++) {
            bboxmin[j] = std::min(bboxmin[j], static_cast<int>(pts2[i][j]));
            bboxmax[j] = std::max(bboxmax[j], static_cast<int>(pts2[i][j]));
        }
// 预处理指令，用于将一个for循环并行化执行，加速程序的执行
#pragma omp parallel for
    // 遍历包围盒每个像素
    for (int x=std::max(bboxmin[0], 0); x<=std::min(bboxmax[0], image.width()-1); x++) {
        for (int y=std::max(bboxmin[1], 0); y<=std::min(bboxmax[1], image.height()-1); y++) {
            vec3 bc_screen = barycentric(pts2, {static_cast<double>(x), static_cast<double>(y)});
            vec3 bc_clip   = {bc_screen.x/pts[0][3], bc_screen.y/pts[1][3], bc_screen.z/pts[2][3]};
            bc_clip = bc_clip/(bc_clip.x+bc_clip.y+bc_clip.z); // check https://github.com/ssloy/tinyrenderer/wiki/Technical-difficulties-linear-interpolation-with-perspective-deformations
            // 使用顶点坐标+重心坐标对P点深度进行插值
            double frag_depth = vec3{clip_verts[0][2], clip_verts[1][2], clip_verts[2][2]}*bc_clip;
            // 像素不在三角形内或者缓存深度小于P点深度则不进行着色,深度值越大,离相机越远
            if (bc_screen.x<0 || bc_screen.y<0 || bc_screen.z<0 || frag_depth > zbuffer[x+y*image.width()]) continue;
            TGAColor color;
            // 如果缓存深度低于P点深度，则更新缓存并着色
            if (shader.fragment(bc_clip, color)) continue; // fragment shader can discard current fragment
            zbuffer[x+y*image.width()] = frag_depth;
            image.set(x, y, color);
        }
    }
}