/**
 * @file panorama_to_perspective.cpp
 * @brief 从 JPG / EXR 全景图提取透视视图，支持常规/多部分/深度 EXR，支持独立 HFOV 和 VFOV
 *
 * 依赖:
 *   - stb_image.h (https://github.com/nothings/stb/blob/master/stb_image.h)
 *   - tinyexr.h   (您提供的版本)
 *
 * 编译:
 *   g++ -std=c++11 -O2 -o pano2persp panorama_to_perspective.cpp -lz -lm
 *
 * 使用示例:
 *   ./pano2persp panorama.jpg out.bmp --yaw 30 --pitch 10
 *   ./pano2persp scene.exr out.bmp --fov 90 --vfov 60   # 独立设置 HFOV=90°, VFOV=60°
 */

//#include <iostream>
//#include <fstream>
//#include <vector>
//#include <string>
//#include <cmath>
//#include <algorithm>
//#include <cstdint>
//#include <cstring>
//#include <memory>
//
//#ifndef M_PI
//#define M_PI 3.14159265358979323846
//#endif
//
//#define STB_IMAGE_IMPLEMENTATION
//#include "stb_image.h"
//
//#define TINYEXR_IMPLEMENTATION
//#include "tinyexr.h"
//
// // ----------------------------------------------------------------------
// // 基础数据结构
// // ----------------------------------------------------------------------
//struct sRGB {
//    uint8_t r, g, b;
//    sRGB() : r(0), g(0), b(0) {}
//    sRGB(uint8_t r_, uint8_t g_, uint8_t b_) : r(r_), g(g_), b(b_) {}
//};
//
//struct Image {
//    int width, height;
//    std::vector<std::vector<sRGB>> data; // data[y][x]
//    Image() : width(0), height(0) {}
//    Image(int w, int h) : width(w), height(h) {
//        data.resize(h, std::vector<sRGB>(w));
//    }
//    sRGB& at(int x, int y) { return data[y][x]; }
//    const sRGB& at(int x, int y) const { return data[y][x]; }
//};
//
//// ----------------------------------------------------------------------
//// 读取 JPG/PNG (8-bit RGB)
//// ----------------------------------------------------------------------
//bool loadImageLDR(const std::string& filename, Image& img) {
//    int w, h, channels;
//    unsigned char* data = stbi_load(filename.c_str(), &w, &h, &channels, 3);
//    if (!data) {
//        std::cerr << "无法加载图像: " << filename << std::endl;
//        return false;
//    }
//    img = Image(w, h);
//    for (int y = 0; y < h; ++y) {
//        for (int x = 0; x < w; ++x) {
//            int idx = (y * w + x) * 3;
//            img.at(x, y) = sRGB(data[idx], data[idx + 1], data[idx + 2]);
//        }
//    }
//    stbi_image_free(data);
//    return true;
//}
//
//// ----------------------------------------------------------------------
//// 将 HDR 浮点数据转换为 8-bit sRGB (Reinhard + Gamma)
//// ----------------------------------------------------------------------
//void hdrToLDR(const float* rgba, int width, int height, Image& out,
//    float exposure, float gamma) {
//    out = Image(width, height);
//    int pixelCount = width * height;
//    // 计算全局最大亮度 (用于更好的色调映射)
//    float maxLum = 0.0f;
//    for (int i = 0; i < pixelCount; ++i) {
//        float r = rgba[4 * i];
//        float g = rgba[4 * i + 1];
//        float b = rgba[4 * i + 2];
//        float lum = std::max({ r, g, b });
//        if (lum > maxLum) maxLum = lum;
//    }
//    if (maxLum < 1e-6f) maxLum = 1.0f;
//
//    for (int y = 0; y < height; ++y) {
//        for (int x = 0; x < width; ++x) {
//            int idx = y * width + x;
//            float r = rgba[4 * idx] * exposure;
//            float g = rgba[4 * idx + 1] * exposure;
//            float b = rgba[4 * idx + 2] * exposure;
//            // Reinhard 色调映射
//            r = r / (1.0f + r);
//            g = g / (1.0f + g);
//            b = b / (1.0f + b);
//            // Gamma 校正
//            float invGamma = 1.0f / gamma;
//            r = std::pow(r, invGamma);
//            g = std::pow(g, invGamma);
//            b = std::pow(b, invGamma);
//            // 钳位并转换为 uint8
//            auto toUint8 = [](float v) -> uint8_t {
//                v = std::max(0.0f, std::min(1.0f, v));
//                return static_cast<uint8_t>(v * 255.0f + 0.5f);
//            };
//            out.at(x, y) = sRGB(toUint8(r), toUint8(g), toUint8(b));
//        }
//    }
//}
//
//// ----------------------------------------------------------------------
//// 读取常规 EXR (单部分)
//// ----------------------------------------------------------------------
//bool loadEXR_Regular(const std::string& filename, Image& img,
//    float exposure, float gamma) {
//    float* rgba = nullptr;
//    int width, height;
//    const char* err = nullptr;
//    int ret = LoadEXR(&rgba, &width, &height, filename.c_str(), &err);
//    if (ret == TINYEXR_SUCCESS) {
//        hdrToLDR(rgba, width, height, img, exposure, gamma);
//        free(rgba);
//        return true;
//    }
//    if (err) FreeEXRErrorMessage(err);
//    return false;
//}
//
//// ----------------------------------------------------------------------
//// 读取多部分 EXR (取第一个包含 RGB 的部分)
//// ----------------------------------------------------------------------
//bool loadEXR_MultiPart(const std::string& filename, Image& img,
//    float exposure, float gamma) {
//    // 解析多部分头
//    EXRVersion version;
//    int ret = ParseEXRVersionFromFile(&version, filename.c_str());
//    if (ret != TINYEXR_SUCCESS) return false;
//
//    EXRHeader** headers = nullptr;
//    int num_headers;
//    const char* err = nullptr;
//    ret = ParseEXRMultipartHeaderFromFile(&headers, &num_headers, &version,
//        filename.c_str(), &err);
//    if (ret != TINYEXR_SUCCESS) {
//        if (err) FreeEXRErrorMessage(err);
//        return false;
//    }
//
//    // 查找包含 RGB 通道的部分 (常见名称: R,G,B 或 r,g,b)
//    int selected_part = -1;
//    for (int i = 0; i < num_headers; ++i) {
//        EXRHeader* hdr = headers[i];
//        bool hasR = false, hasG = false, hasB = false;
//        for (int c = 0; c < hdr->num_channels; ++c) {
//            std::string ch_name(hdr->channels[c].name);
//            if (ch_name == "R" || ch_name == "r") hasR = true;
//            if (ch_name == "G" || ch_name == "g") hasG = true;
//            if (ch_name == "B" || ch_name == "b") hasB = true;
//        }
//        if (hasR && hasG && hasB) {
//            selected_part = i;
//            break;
//        }
//    }
//    if (selected_part == -1) {
//        // 取第一个部分
//        selected_part = 0;
//    }
//
//    // 设置请求的像素类型为 float
//    for (int i = 0; i < num_headers; ++i) {
//        EXRHeader* hdr = headers[i];
//        for (int c = 0; c < hdr->num_channels; ++c) {
//            hdr->requested_pixel_types[c] = TINYEXR_PIXELTYPE_FLOAT;
//        }
//    }
//
//    // 加载图像
//    EXRImage* images = new EXRImage[num_headers];
//    for (int i = 0; i < num_headers; ++i) InitEXRImage(&images[i]);
//    ret = LoadEXRMultipartImageFromFile(images, (const EXRHeader**)headers,
//        num_headers, filename.c_str(), &err);
//    if (ret != TINYEXR_SUCCESS) {
//        if (err) FreeEXRErrorMessage(err);
//        for (int i = 0; i < num_headers; ++i) FreeEXRHeader(headers[i]);
//        free(headers);
//        delete[] images;
//        return false;
//    }
//
//    // 从选中的部分提取 RGB 数据
//    EXRImage& imgPart = images[selected_part];
//    int width = imgPart.width;
//    int height = imgPart.height;
//
//    // 查找 R,G,B 通道索引
//    EXRHeader* selHdr = headers[selected_part];
//    int idxR = -1, idxG = -1, idxB = -1;
//    for (int c = 0; c < selHdr->num_channels; ++c) {
//        std::string ch_name(selHdr->channels[c].name);
//        if (ch_name == "R" || ch_name == "r") idxR = c;
//        if (ch_name == "G" || ch_name == "g") idxG = c;
//        if (ch_name == "B" || ch_name == "b") idxB = c;
//    }
//    if (idxR == -1 || idxG == -1 || idxB == -1) {
//        std::cerr << "所选部分没有 RGB 通道" << std::endl;
//        // 清理
//        for (int i = 0; i < num_headers; ++i) FreeEXRImage(&images[i]);
//        for (int i = 0; i < num_headers; ++i) FreeEXRHeader(headers[i]);
//        free(headers);
//        delete[] images;
//        return false;
//    }
//
//    // 构造 RGBA 临时数组
//    int pixelCount = width * height;
//    std::vector<float> rgba(pixelCount * 4, 0.0f);
//    float** imgPtrs = (float**)imgPart.images;
//    for (int y = 0; y < height; ++y) {
//        for (int x = 0; x < width; ++x) {
//            int idx = y * width + x;
//            rgba[4 * idx + 0] = imgPtrs[idxR][idx];
//            rgba[4 * idx + 1] = imgPtrs[idxG][idx];
//            rgba[4 * idx + 2] = imgPtrs[idxB][idx];
//            rgba[4 * idx + 3] = 1.0f; // Alpha 填充 1
//        }
//    }
//
//    hdrToLDR(rgba.data(), width, height, img, exposure, gamma);
//
//    // 清理
//    for (int i = 0; i < num_headers; ++i) FreeEXRImage(&images[i]);
//    for (int i = 0; i < num_headers; ++i) FreeEXRHeader(headers[i]);
//    free(headers);
//    delete[] images;
//    return true;
//}
//
//// ----------------------------------------------------------------------
//// 读取深度 EXR (取每个像素的第一个样本)
//// ----------------------------------------------------------------------
//bool loadEXR_Deep(const std::string& filename, Image& img,
//    float exposure, float gamma) {
//    DeepImage deep;
//    const char* err = nullptr;
//    int ret = LoadDeepEXR(&deep, filename.c_str(), &err);
//    if (ret != TINYEXR_SUCCESS) {
//        if (err) FreeEXRErrorMessage(err);
//        return false;
//    }
//
//    // 查找 RGB 通道索引
//    int idxR = -1, idxG = -1, idxB = -1;
//    for (int c = 0; c < deep.num_channels; ++c) {
//        std::string ch_name(deep.channel_names[c]);
//        if (ch_name == "R" || ch_name == "r") idxR = c;
//        if (ch_name == "G" || ch_name == "g") idxG = c;
//        if (ch_name == "B" || ch_name == "b") idxB = c;
//    }
//    if (idxR == -1 || idxG == -1 || idxB == -1) {
//        std::cerr << "深度 EXR 中没有 RGB 通道" << std::endl;
//        // 清理 DeepImage (需要手动释放)
//        for (int c = 0; c < deep.num_channels; ++c) {
//            for (int y = 0; y < deep.height; ++y) {
//                free(deep.image[c][y]);
//            }
//            free(deep.image[c]);
//            free((void*)deep.channel_names[c]);
//        }
//        free(deep.image);
//        for (int y = 0; y < deep.height; ++y) free(deep.offset_table[y]);
//        free(deep.offset_table);
//        return false;
//    }
//
//    int width = deep.width, height = deep.height;
//    std::vector<float> rgba(width * height * 4, 0.0f);
//    for (int y = 0; y < height; ++y) {
//        for (int x = 0; x < width; ++x) {
//            int sampleCount = deep.offset_table[y][x];
//            if (sampleCount > 0) {
//                // 取第一个样本
//                int idx = y * width + x;
//                rgba[4 * idx + 0] = deep.image[idxR][y][0];
//                rgba[4 * idx + 1] = deep.image[idxG][y][0];
//                rgba[4 * idx + 2] = deep.image[idxB][y][0];
//                rgba[4 * idx + 3] = 1.0f;
//            }
//            else {
//                // 无样本 -> 黑色
//                int idx = y * width + x;
//                rgba[4 * idx + 0] = 0.0f;
//                rgba[4 * idx + 1] = 0.0f;
//                rgba[4 * idx + 2] = 0.0f;
//                rgba[4 * idx + 3] = 1.0f;
//            }
//        }
//    }
//
//    hdrToLDR(rgba.data(), width, height, img, exposure, gamma);
//
//    // 清理 DeepImage
//    for (int c = 0; c < deep.num_channels; ++c) {
//        for (int y = 0; y < deep.height; ++y) {
//            free(deep.image[c][y]);
//        }
//        free(deep.image[c]);
//        free((void*)deep.channel_names[c]);
//    }
//    free(deep.image);
//    for (int y = 0; y < deep.height; ++y) free(deep.offset_table[y]);
//    free(deep.offset_table);
//    return true;
//}
//
//// ----------------------------------------------------------------------
//// 通用 EXR 加载接口 (自动尝试常规 -> 多部分 -> 深度)
//// ----------------------------------------------------------------------
//bool loadImageEXR(const std::string& filename, Image& img,
//    float exposure, float gamma) {
//    if (loadEXR_Regular(filename, img, exposure, gamma)) {
//        std::cout << "EXR 加载 (常规) 成功" << std::endl;
//        return true;
//    }
//    std::cout << "尝试多部分 EXR..." << std::endl;
//    if (loadEXR_MultiPart(filename, img, exposure, gamma)) {
//        std::cout << "EXR 加载 (多部分) 成功" << std::endl;
//        return true;
//    }
//    std::cout << "尝试深度 EXR..." << std::endl;
//    if (loadEXR_Deep(filename, img, exposure, gamma)) {
//        std::cout << "EXR 加载 (深度) 成功" << std::endl;
//        return true;
//    }
//    std::cerr << "所有 EXR 加载方式均失败" << std::endl;
//    return false;
//}
//
//// ----------------------------------------------------------------------
//// BMP 写入 (24位，负高度实现顶行优先)
//// ----------------------------------------------------------------------
//bool writeBMP(const std::string& filename, const Image& img) {
//    std::ofstream file(filename, std::ios::binary);
//    if (!file) return false;
//    int w = img.width, h = img.height;
//    int rowSize = (w * 3 + 3) & ~3;
//    int imageSize = rowSize * h;
//    int fileSize = 54 + imageSize;
//    uint8_t header[54] = { 0 };
//    header[0] = 'B'; header[1] = 'M';
//    *reinterpret_cast<int*>(&header[2]) = fileSize;
//    *reinterpret_cast<int*>(&header[10]) = 54;
//    *reinterpret_cast<int*>(&header[14]) = 40;
//    *reinterpret_cast<int*>(&header[18]) = w;
//    *reinterpret_cast<int*>(&header[22]) = -h;   // 负高度表示顶行优先
//    *reinterpret_cast<short*>(&header[26]) = 1;
//    *reinterpret_cast<short*>(&header[28]) = 24;
//    *reinterpret_cast<int*>(&header[34]) = imageSize;
//    file.write(reinterpret_cast<char*>(header), 54);
//    std::vector<uint8_t> row(rowSize, 0);
//    for (int y = 0; y < h; ++y) {
//        for (int x = 0; x < w; ++x) {
//            const sRGB& rgb = img.at(x, y);
//            row[x * 3] = rgb.b;
//            row[x * 3 + 1] = rgb.g;
//            row[x * 3 + 2] = rgb.r;
//        }
//        file.write(reinterpret_cast<char*>(row.data()), rowSize);
//        if (!file) return false;
//    }
//    return true;
//}
//
//// ----------------------------------------------------------------------
//// 全景图双线性采样 (u,v ∈ [0,1], 经度方向环绕)
//// ----------------------------------------------------------------------
//sRGB samplePanoramaBilinear(const Image& pano, double u, double v) {
//    double px = u * (pano.width - 1);
//    double py = v * (pano.height - 1);
//    int x0 = static_cast<int>(px);
//    int y0 = static_cast<int>(py);
//    int x1 = x0 + 1;
//    int y1 = y0 + 1;
//    x0 = (x0 % pano.width + pano.width) % pano.width;
//    x1 = (x1 % pano.width + pano.width) % pano.width;
//    y0 = std::max(0, std::min(y0, pano.height - 1));
//    y1 = std::max(0, std::min(y1, pano.height - 1));
//    double dx = px - x0;
//    double dy = py - y0;
//    const sRGB& c00 = pano.at(x0, y0);
//    const sRGB& c10 = pano.at(x1, y0);
//    const sRGB& c01 = pano.at(x0, y1);
//    const sRGB& c11 = pano.at(x1, y1);
//    double r = (1 - dx) * (1 - dy) * c00.r + dx * (1 - dy) * c10.r + (1 - dx) * dy * c01.r + dx * dy * c11.r;
//    double g = (1 - dx) * (1 - dy) * c00.g + dx * (1 - dy) * c10.g + (1 - dx) * dy * c01.g + dx * dy * c11.g;
//    double b = (1 - dx) * (1 - dy) * c00.b + dx * (1 - dy) * c10.b + (1 - dx) * dy * c01.b + dx * dy * c11.b;
//    return sRGB(static_cast<uint8_t>(r + 0.5), static_cast<uint8_t>(g + 0.5), static_cast<uint8_t>(b + 0.5));
//}
//
//// ----------------------------------------------------------------------
//// 射线与单位球面求交
//// ----------------------------------------------------------------------
//bool raySphereIntersection(const double origin[3], const double dir[3],
//    double& hit_u, double& hit_v) {
//    double a = dir[0] * dir[0] + dir[1] * dir[1] + dir[2] * dir[2];
//    double b = 2.0 * (origin[0] * dir[0] + origin[1] * dir[1] + origin[2] * dir[2]);
//    double c = origin[0] * origin[0] + origin[1] * origin[1] + origin[2] * origin[2] - 1.0;
//    double disc = b * b - 4.0 * a * c;
//    if (disc < 0.0) return false;
//    double sqrt_disc = sqrt(disc);
//    double t1 = (-b - sqrt_disc) / (2.0 * a);
//    double t2 = (-b + sqrt_disc) / (2.0 * a);
//    double t = (t1 > 1e-6) ? t1 : ((t2 > 1e-6) ? t2 : -1.0);
//    if (t <= 1e-6) return false;
//    double hit_x = origin[0] + t * dir[0];
//    double hit_y = origin[1] + t * dir[1];
//    double hit_z = origin[2] + t * dir[2];
//    double theta = acos(hit_y);
//    double phi = atan2(hit_x, hit_z);
//    hit_u = (phi + M_PI) / (2.0 * M_PI);
//    hit_v = theta / M_PI;
//    return true;
//}
//
//// ----------------------------------------------------------------------
//// 从全景图生成透视视图（支持相机位置偏移，独立 HFOV / VFOV）
//// ----------------------------------------------------------------------
//Image perspectiveFromPanorama(const Image& pano,
//    double cx, double cy, double cz,
//    double yaw_deg, double pitch_deg, double roll_deg,
//    double hfov_deg, double vfov_deg,
//    int outW, int outH, int aa) {
//    Image output(outW, outH);
//    if (pano.width == 0 || pano.height == 0) return output;
//
//    double cameraPos[3] = { cx, cy, cz };
//    double yaw = yaw_deg * M_PI / 180.0;
//    double pitch = pitch_deg * M_PI / 180.0;
//    double roll = roll_deg * M_PI / 180.0;
//    double cyaw = cos(yaw), syaw = sin(yaw);
//    double cp = cos(pitch), sp = sin(pitch);
//    double cr = cos(roll), sr = sin(roll);
//    double R[3][3] = {
//        { cyaw * cp,  cyaw * sp * sr - syaw * cr,  cyaw * sp * cr + syaw * sr },
//        { syaw * cp,  syaw * sp * sr + cyaw * cr,  syaw * sp * cr - cyaw * sr },
//        { -sp,      cp * sr,                  cp * cr }
//    };
//
//    // 计算水平和垂直方向的焦距
//    double hfov_rad = hfov_deg * M_PI / 180.0;
//    double focalX = (outW / 2.0) / tan(hfov_rad / 2.0);
//
//    double focalY;
//    if (vfov_deg > 0.0) {
//        // 用户指定了垂直视场角
//        double vfov_rad = vfov_deg * M_PI / 180.0;
//        focalY = (outH / 2.0) / tan(vfov_rad / 2.0);
//    }
//    else {
//        // 未指定时，保持像素纵横比与图像宽高比一致（即与之前行为相同）
//        focalY = focalX * (static_cast<double>(outH) / outW);
//    }
//
//    double halfW = outW / 2.0;
//    double halfH = outH / 2.0;
//
//    int samples = aa * aa;
//    double step = 1.0 / aa;
//
//    for (int y = 0; y < outH; ++y) {
//        for (int x = 0; x < outW; ++x) {
//            double r_sum = 0.0, g_sum = 0.0, b_sum = 0.0;
//            int valid = 0;
//            for (int sy = 0; sy < aa; ++sy) {
//                for (int sx = 0; sx < aa; ++sx) {
//                    double offX = (sx + 0.5) * step - 0.5;
//                    double offY = (sy + 0.5) * step - 0.5;
//                    double nx = (x + offX - halfW) / focalX;
//                    double ny = (y + offY - halfH) / focalY;
//                    double nz = 1.0;
//                    double len = sqrt(nx * nx + ny * ny + nz * nz);
//                    nx /= len; ny /= len; nz /= len;
//                    // 转换到世界坐标系
//                    double wx = R[0][0] * nx + R[0][1] * ny + R[0][2] * nz;
//                    double wy = R[1][0] * nx + R[1][1] * ny + R[1][2] * nz;
//                    double wz = R[2][0] * nx + R[2][1] * ny + R[2][2] * nz;
//                    len = sqrt(wx * wx + wy * wy + wz * wz);
//                    wx /= len; wy /= len; wz /= len;
//                    double dir[3] = { wx, wy, wz };
//                    double u, v;
//                    if (raySphereIntersection(cameraPos, dir, u, v)) {
//                        if (u >= 0 && u <= 1 && v >= 0 && v <= 1) {
//                            sRGB col = samplePanoramaBilinear(pano, u, v);
//                            r_sum += col.r; g_sum += col.g; b_sum += col.b;
//                            valid++;
//                        }
//                    }
//                }
//            }
//            if (valid > 0) {
//                output.at(x, y) = sRGB(static_cast<uint8_t>(r_sum / valid + 0.5),
//                    static_cast<uint8_t>(g_sum / valid + 0.5),
//                    static_cast<uint8_t>(b_sum / valid + 0.5));
//            }
//            else {
//                output.at(x, y) = sRGB(0, 0, 0);
//            }
//        }
//    }
//    return output;
//}
//
//// ----------------------------------------------------------------------
//// 帮助信息
//// ----------------------------------------------------------------------
//void printHelp(const char* prog) {
//    std::cout << "用法: " << prog << " <input.(jpg|exr)> <output.bmp> [options]\n"
//        << "相机位置 (球面半径=1):\n"
//        << "  --cx <val>          X 坐标 (默认: 0.0)\n"
//        << "  --cy <val>          Y 坐标 (默认: 0.0)\n"
//        << "  --cz <val>          Z 坐标 (默认: 0.0)\n"
//        << "相机朝向:\n"
//        << "  --yaw <deg>         偏航角 (默认: 0)\n"
//        << "  --pitch <deg>       俯仰角 (默认: 0)\n"
//        << "  --roll <deg>        横滚角 (默认: 0)\n"
//        << "透视参数:\n"
//        << "  --fov <deg>         水平视场角 (默认: 90)\n"
//        << "  --vfov <deg>        垂直视场角 (可选, 未指定时由图像宽高比自动决定)\n"
//        << "  --width <px>        输出宽度 (默认: 800)\n"
//        << "  --height <px>       输出高度 (默认: 600)\n"
//        << "  --aa <N>            超采样 NxN (默认: 2)\n"
//        << "EXR 选项:\n"
//        << "  --exposure <val>    曝光补偿 (默认: 1.0)\n"
//        << "  --gamma <val>       伽马校正值 (默认: 2.2)\n"
//        << "  --help              显示帮助\n"
//        << "\n示例:\n"
//        << "  " << prog << " pano.jpg view.bmp --yaw 30 --pitch 10\n"
//        << "  " << prog << " scene.exr view.bmp --exposure 2.0 --gamma 2.4 --fov 90 --vfov 60\n";
//}
//
//// ----------------------------------------------------------------------
//// 主函数
//// ----------------------------------------------------------------------
//int main(int argc, char* argv[]) {
//    if (argc < 3) {
//        printHelp(argv[0]);
//        return 1;
//    }
//
//    std::string inputFile = argv[1];
//    std::string outputFile = argv[2];
//
//    double cx = 0.0, cy = 0.0, cz = 0.0;
//    double yaw = 0.0, pitch = 0.0, roll = 0.0;
//    double hfov = 90.0, vfov = -1.0;   // vfov = -1 表示未指定
//    int outW = 800, outH = 600;
//    int aa = 2;
//    float exposure = 1.0f;
//    float gamma = 2.2f;
//
//    for (int i = 3; i < argc; ++i) {
//        std::string arg = argv[i];
//        if (arg == "--cx" && i + 1 < argc) cx = std::stod(argv[++i]);
//        else if (arg == "--cy" && i + 1 < argc) cy = std::stod(argv[++i]);
//        else if (arg == "--cz" && i + 1 < argc) cz = std::stod(argv[++i]);
//        else if (arg == "--yaw" && i + 1 < argc) yaw = std::stod(argv[++i]);
//        else if (arg == "--pitch" && i + 1 < argc) pitch = std::stod(argv[++i]);
//        else if (arg == "--roll" && i + 1 < argc) roll = std::stod(argv[++i]);
//        else if (arg == "--fov" && i + 1 < argc) hfov = std::stod(argv[++i]);
//        else if (arg == "--vfov" && i + 1 < argc) vfov = std::stod(argv[++i]);
//        else if (arg == "--width" && i + 1 < argc) outW = std::stoi(argv[++i]);
//        else if (arg == "--height" && i + 1 < argc) outH = std::stoi(argv[++i]);
//        else if (arg == "--aa" && i + 1 < argc) { aa = std::stoi(argv[++i]); if (aa < 1) aa = 1; }
//        else if (arg == "--exposure" && i + 1 < argc) exposure = std::stof(argv[++i]);
//        else if (arg == "--gamma" && i + 1 < argc) gamma = std::stof(argv[++i]);
//        else if (arg == "--help") { printHelp(argv[0]); return 0; }
//        else {
//            std::cerr << "未知选项: " << arg << std::endl;
//            printHelp(argv[0]);
//            return 1;
//        }
//    }
//
//    Image panorama;
//    std::string ext = inputFile.substr(inputFile.find_last_of('.') + 1);
//    std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
//    bool ok = false;
//    if (ext == "exr") {
//        ok = loadImageEXR(inputFile, panorama, exposure, gamma);
//        if (ok) std::cout << "EXR 加载成功, 尺寸: " << panorama.width << "x" << panorama.height << std::endl;
//    }
//    else if (ext == "jpg" || ext == "jpeg" || ext == "png" || ext == "bmp" || ext == "tga") {
//        ok = loadImageLDR(inputFile, panorama);
//        if (ok) std::cout << "LDR 图像加载成功, 尺寸: " << panorama.width << "x" << panorama.height << std::endl;
//    }
//    else {
//        std::cerr << "不支持的文件格式: " << ext << std::endl;
//        return 1;
//    }
//    if (!ok) return 1;
//
//    std::cout << "相机位置: (" << cx << ", " << cy << ", " << cz << ")\n";
//    std::cout << "相机朝向: yaw=" << yaw << "°, pitch=" << pitch << "°, roll=" << roll << "°\n";
//    std::cout << "透视参数: HFOV=" << hfov << "°, VFOV="
//        << (vfov > 0 ? std::to_string(vfov) : "auto")
//        << ", 输出=" << outW << "x" << outH << ", aa=" << aa << std::endl;
//
//    Image view = perspectiveFromPanorama(panorama, cx, cy, cz, yaw, pitch, roll, hfov, vfov, outW, outH, aa);
//    if (!writeBMP(outputFile, view)) {
//        std::cerr << "写入 BMP 失败" << std::endl;
//        return 1;
//    }
//    std::cout << "透视视图已保存: " << outputFile << std::endl;
//    return 0;
//}

#include "mainwindow.h"
#include <QApplication>
#include <QSurfaceFormat>

int main(int argc, char *argv[])
{
    QSurfaceFormat format;
    format.setVersion(3, 3);
    format.setProfile(QSurfaceFormat::CoreProfile);
    format.setDepthBufferSize(24);
    format.setStencilBufferSize(8);
    format.setSamples(4);
    QSurfaceFormat::setDefaultFormat(format);

    QApplication a(argc, argv);
    MainWindow w;
    w.show();
    return a.exec();
}