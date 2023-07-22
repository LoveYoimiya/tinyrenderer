#include "model.h"
#include "our_gl.h"
#include <limits>
#include <iostream>
// 屏幕宽高，constexpr关键字表示声明一个可以在编译器计算的常量表达式
constexpr int width  = 800;
constexpr int height = 800;
// 光照方向 
constexpr vec3 light_dir{1, 1, 1};
// 相机坐标
constexpr vec3 eye{1, 1, 3};
// 本地坐标系原点坐标
constexpr vec3 center{0, 0, 0};
// 相机朝上方向向量
constexpr vec3 up{0, 1, 0};

// extern用于声明外部变量和函数
extern mat<4, 4> ModelView;
extern mat<4, 4> Projection;
extern mat<4, 4> Viewport;

std::vector<double> shadowbuffer(width*height, std::numeric_limits<double>::max());

struct Shader : public IShader {
    // 使用的模型
    const Model &model;
    // 在相机坐标系中的光照方向
    vec3 uniform_l;

    mat<3, 3> view_tri;
    // 顶点贴图纹理坐标
    mat<2, 3> varying_uv;
    // 顶点法线向量
    mat<3, 3> varying_nrm;

    Shader(const Model &m) : model(m){
        uniform_l = proj<3>((ModelView * embed<4>(light_dir, 0.))).normalized();
    }

    /**
     * @brief 顶点着色器
     * 
     * @param iface 三角形面索引
     * @param nthvert 面的顶点索引(0~2)
     * @param gl_Position 顶点在相机坐标系的坐标
     */
    virtual void vertex(const int iface, const int nthvert, vec4& gl_Position) {
        varying_uv.set_col(nthvert, model.uv(iface, nthvert));
        // 法线向量转换到相机坐标系需要乘以ModelView的逆转置
        varying_nrm.set_col(nthvert, proj<3>((ModelView).invert_transpose()*embed<4>(model.normal(iface, nthvert), 0.)));
        // 先对模型进行坐标系的转换
        gl_Position = ModelView * embed<4>(model.vert(iface, nthvert));
        view_tri.set_col(nthvert, proj<3>(gl_Position));
        // 进行透视投影
        gl_Position = Projection * gl_Position;
    }

    /**
     * @brief 片段着色器
     * 
     * @param bar 重心坐标(alpha, beta, gamma)
     * @param gl_FragColor 最终要得到的颜色
     * @return true 
     * @return false 
     */
    virtual bool fragment(const vec3 bar, TGAColor &gl_FragColor) {
        // 插值获得当前像素点的法线向量和纹理坐标
        vec3 bn = (varying_nrm * bar).normalized();
        vec2 uv = varying_uv * bar;
        // 法线转换到切线空间
        mat<3, 3> AI = mat<3, 3>{ {view_tri.col(1) - view_tri.col(0), view_tri.col(2) - view_tri.col(0), bn}}.invert();
        vec3 i = AI * vec3{varying_uv[0][1] - varying_uv[0][0], varying_uv[0][2] - varying_uv[0][0], 0};
        vec3 j = AI * vec3{varying_uv[1][1] - varying_uv[1][0], varying_uv[1][2] - varying_uv[1][0], 0};
        mat<3,3> B = mat<3,3>{ {i.normalized(), j.normalized(), bn} }.transpose();

        vec3 n = (B * model.normal(uv)).normalized();

        // 漫反射项，入射方向和法线方向的夹角cosine
        double diff = std::max(0., n*uniform_l);
        // 通过入射方向和法线方向获取出射方向
        vec3 r = (n * (n * uniform_l * 2.f) - uniform_l).normalized();
        // 高光项，阶数存储在spec贴图中
        double spec = std::pow(std::max(-r.z, 0.), 5+sample2D(model.specular(), uv)[0]);
        TGAColor c = sample2D(model.diffuse(), uv);
        for (int i = 0; i < 3; i++) {
            // 10 为设置的环境光， phong reflection = ambient + diffuse + specular
            gl_FragColor[i] = std::min<int>(10 + c[i] * (diff + spec), 255);
        }
        return false;
    }
};

int main(int argc, char** argv) {
    if (2 > argc) {
        std::cerr << "Usage: " << argv[0] << "obj/model.obj" << std::endl;
    }
    // 创造屏幕对象
    TGAImage framebuffer(width, height, TGAImage::RGB);

    TGAImage depth(width, height, TGAImage::RGB);
    // 初始化zbuffer为正无穷
    std::vector<double> zbuffer(width*height, std::numeric_limits<double>::max());

    for (int m = 1; m < argc; m++)
    {
        Model model(argv[m]);
        lookat(eye, center, up);
        viewport(width / 8, height / 8, width * 3 / 4, height * 3 / 4);
        projection((eye - center).norm());

        Shader shader(model);
        // nfaces代表三角形面的个数
        for (int i = 0; i < model.nfaces(); i++) {
            // 存储顶点坐标
            vec4 clip_vert[3];
            for (int j = 0; j < 3; j++) {
                shader.vertex(i, j, clip_vert[j]);
            }
            triangle(clip_vert, shader, framebuffer, zbuffer);
        }
    }
    framebuffer.write_tga_file("framebuffer.tga");
    return 0;
}
