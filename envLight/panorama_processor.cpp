#include "panorama_processor.h"
#include "CameraTransform.h"
#include <cmath>
#include <algorithm>
#include <iostream>
#include <fstream>
#include <cstring>
#include <vector>
#include <cctype>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

// 定义STB_IMAGE实现
#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"

//#define TINYEXR_USE_MINIZ 0
#define TINYEXR_IMPLEMENTATION
#include "tinyexr.h"

#ifdef _WIN32
#include <windows.h> // for MultiByteToWideChar
#else
#include <cstdio>
#endif
#include <QPointF>
#include <optional>
// ----------------------------------------------------------------------
// 内部辅助函数（匿名命名空间）
// ----------------------------------------------------------------------
namespace {

	inline double dot3(const double a[3], const double b[3])
	{
		return a[0] * b[0]
			+ a[1] * b[1]
			+ a[2] * b[2];
	}

	inline void cross3(
		const double a[3],
		const double b[3],
		double r[3])
	{
		r[0] = a[1] * b[2] - a[2] * b[1];
		r[1] = a[2] * b[0] - a[0] * b[2];
		r[2] = a[0] * b[1] - a[1] * b[0];
	}

	inline bool normalize3(double v[3])
	{
		const double len =
			std::sqrt(v[0] * v[0] +
				v[1] * v[1] +
				v[2] * v[2]);

		if (len < 1e-10)
			return false;

		v[0] /= len;
		v[1] /= len;
		v[2] /= len;

		return true;
	}

    float srgbToLinear(float c) {
        c = std::max(0.0f, std::min(1.0f, c));
        if (c <= 0.04045f) return c / 12.92f;
        return std::pow((c + 0.055f) / 1.055f, 2.4f);
    }

    float linearToSRGB(float c, float gamma) {
        c = std::max(0.0f, std::min(1.0f, c));
        // Keep the historical gamma parameter useful while using the proper sRGB curve
        // for the default gamma value.
        if (std::abs(gamma - 2.2f) < 1e-4f) {
            if (c <= 0.0031308f) return 12.92f * c;
            return 1.055f * std::pow(c, 1.0f / 2.4f) - 0.055f;
        }
        return std::pow(c, 1.0f / std::max(gamma, 1e-6f));
    }

    void printHDRStats(const char* tag, const HDRImage& img) {
        if (img.width <= 0 || img.height <= 0 || img.data.empty()) return;
        float maxR = 0.0f, maxG = 0.0f, maxB = 0.0f, maxLum = 0.0f;
        double sumLum = 0.0;
        size_t valid = 0;
        for (size_t i = 0; i < img.data.size(); ++i) {
            const LinearRGB& p = img.data[i];
            if (!std::isfinite(p.r) || !std::isfinite(p.g) || !std::isfinite(p.b)) continue;
            maxR = std::max(maxR, p.r);
            maxG = std::max(maxG, p.g);
            maxB = std::max(maxB, p.b);
            float lum = 0.2126f * p.r + 0.7152f * p.g + 0.0722f * p.b;
            maxLum = std::max(maxLum, lum);
            sumLum += lum;
            ++valid;
        }
        std::cout << tag << ": " << img.width << "x" << img.height
                  << ", maxRGB=(" << maxR << ", " << maxG << ", " << maxB << ")"
                  << ", maxLum=" << maxLum
                  << ", meanLum=" << (valid ? sumLum / static_cast<double>(valid) : 0.0)
                  << std::endl;
    }

    // Regular EXR: preserve TinyEXR float values exactly (no tone mapping here).
    bool loadEXR_Regular(const std::string& filename, HDRImage& img) {
        float* rgba = nullptr;
        int width = 0, height = 0;
        const char* err = nullptr;
        int ret = LoadEXR(&rgba, &width, &height, filename.c_str(), &err);
        if (ret == TINYEXR_SUCCESS) {
            img = HDRImage(width, height);
            for (int y = 0; y < height; ++y) {
                for (int x = 0; x < width; ++x) {
                    const size_t idx = static_cast<size_t>(y) * static_cast<size_t>(width) + static_cast<size_t>(x);
                    img.at(x, y) = LinearRGB(rgba[4 * idx + 0], rgba[4 * idx + 1], rgba[4 * idx + 2]);
                }
            }
            free(rgba);
            printHDRStats("EXR loaded", img);
            return true;
        }
        if (err) FreeEXRErrorMessage(err);
        return false;
    }

    // Multipart EXR: choose the first part containing RGB, request FLOAT and preserve it.
    bool loadEXR_MultiPart(const std::string& filename, HDRImage& img) {
        EXRVersion version;
        if (ParseEXRVersionFromFile(&version, filename.c_str()) != TINYEXR_SUCCESS)
            return false;

        EXRHeader** headers = nullptr;
        int num_headers = 0;
        const char* err = nullptr;
        if (ParseEXRMultipartHeaderFromFile(&headers, &num_headers, &version,
            filename.c_str(), &err) != TINYEXR_SUCCESS) {
            if (err) FreeEXRErrorMessage(err);
            return false;
        }

        int selected_part = -1;
        for (int i = 0; i < num_headers; ++i) {
            EXRHeader* hdr = headers[i];
            bool hasR = false, hasG = false, hasB = false;
            for (int c = 0; c < hdr->num_channels; ++c) {
                std::string ch_name(hdr->channels[c].name);
                if (ch_name == "R" || ch_name == "r") hasR = true;
                if (ch_name == "G" || ch_name == "g") hasG = true;
                if (ch_name == "B" || ch_name == "b") hasB = true;
            }
            if (hasR && hasG && hasB) { selected_part = i; break; }
        }
        if (selected_part == -1) selected_part = 0;

        for (int i = 0; i < num_headers; ++i) {
            EXRHeader* hdr = headers[i];
            for (int c = 0; c < hdr->num_channels; ++c)
                hdr->requested_pixel_types[c] = TINYEXR_PIXELTYPE_FLOAT;
        }

        EXRImage* images = new EXRImage[num_headers];
        for (int i = 0; i < num_headers; ++i) InitEXRImage(&images[i]);
        int ret = LoadEXRMultipartImageFromFile(images, (const EXRHeader**)headers,
            num_headers, filename.c_str(), &err);
        if (ret != TINYEXR_SUCCESS) {
            if (err) FreeEXRErrorMessage(err);
            for (int i = 0; i < num_headers; ++i) FreeEXRHeader(headers[i]);
            free(headers);
            delete[] images;
            return false;
        }

        EXRImage& imgPart = images[selected_part];
        int width = imgPart.width, height = imgPart.height;
        EXRHeader* selHdr = headers[selected_part];
        int idxR = -1, idxG = -1, idxB = -1;
        for (int c = 0; c < selHdr->num_channels; ++c) {
            std::string ch_name(selHdr->channels[c].name);
            if (ch_name == "R" || ch_name == "r") idxR = c;
            if (ch_name == "G" || ch_name == "g") idxG = c;
            if (ch_name == "B" || ch_name == "b") idxB = c;
        }
        if (idxR == -1 || idxG == -1 || idxB == -1) {
            for (int i = 0; i < num_headers; ++i) FreeEXRImage(&images[i]);
            for (int i = 0; i < num_headers; ++i) FreeEXRHeader(headers[i]);
            free(headers);
            delete[] images;
            return false;
        }

        img = HDRImage(width, height);
        float** imgPtrs = reinterpret_cast<float**>(imgPart.images);
        for (int y = 0; y < height; ++y) {
            for (int x = 0; x < width; ++x) {
                int idx = y * width + x;
                img.at(x, y) = LinearRGB(imgPtrs[idxR][idx], imgPtrs[idxG][idx], imgPtrs[idxB][idx]);
            }
        }

        for (int i = 0; i < num_headers; ++i) FreeEXRImage(&images[i]);
        for (int i = 0; i < num_headers; ++i) FreeEXRHeader(headers[i]);
        free(headers);
        delete[] images;
        printHDRStats("Multipart EXR loaded", img);
        return true;
    }

    // Deep EXR: preserve the first sample per pixel, matching the previous project behavior.
    bool loadEXR_Deep(const std::string& filename, HDRImage& img) {
        DeepImage deep;
        const char* err = nullptr;
        int ret = LoadDeepEXR(&deep, filename.c_str(), &err);
        if (ret != TINYEXR_SUCCESS) {
            if (err) FreeEXRErrorMessage(err);
            return false;
        }

        int idxR = -1, idxG = -1, idxB = -1;
        for (int c = 0; c < deep.num_channels; ++c) {
            std::string ch_name(deep.channel_names[c]);
            if (ch_name == "R" || ch_name == "r") idxR = c;
            if (ch_name == "G" || ch_name == "g") idxG = c;
            if (ch_name == "B" || ch_name == "b") idxB = c;
        }
        if (idxR == -1 || idxG == -1 || idxB == -1) {
            for (int c = 0; c < deep.num_channels; ++c) {
                for (int y = 0; y < deep.height; ++y) free(deep.image[c][y]);
                free(deep.image[c]);
                free((void*)deep.channel_names[c]);
            }
            free(deep.image);
            for (int y = 0; y < deep.height; ++y) free(deep.offset_table[y]);
            free(deep.offset_table);
            return false;
        }

        int width = deep.width, height = deep.height;
        img = HDRImage(width, height);
        for (int y = 0; y < height; ++y) {
            for (int x = 0; x < width; ++x) {
                // TinyEXR stores cumulative sample counts per scanline.
                int sampleEnd = deep.offset_table[y][x];
                int sampleStart = (x == 0) ? 0 : deep.offset_table[y][x - 1];
                int sampleCount = sampleEnd - sampleStart;
                if (sampleCount > 0) {
                    // Preserve the project's previous policy of using the first deep sample,
                    // but index the correct sample for each pixel.
                    img.at(x, y) = LinearRGB(
                        deep.image[idxR][y][sampleStart],
                        deep.image[idxG][y][sampleStart],
                        deep.image[idxB][y][sampleStart]);
                } else {
                    img.at(x, y) = LinearRGB();
                }
            }
        }

        for (int c = 0; c < deep.num_channels; ++c) {
            for (int y = 0; y < deep.height; ++y) free(deep.image[c][y]);
            free(deep.image[c]);
            free((void*)deep.channel_names[c]);
        }
        free(deep.image);
        for (int y = 0; y < deep.height; ++y) free(deep.offset_table[y]);
        free(deep.offset_table);
        printHDRStats("Deep EXR loaded", img);
        return true;
    }

    // 射线与单位球面求交
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

    //    // ENU world: +X East, +Y North, +Z Up.
    //    hit_z = std::max(-1.0, std::min(1.0, hit_z));
    //    const double azimuth = atan2(hit_x, hit_y);
    //    hit_u = (azimuth + M_PI) / (2.0 * M_PI);
    //    hit_v = acos(hit_z) / M_PI;
    //    return true;
    //}

	bool raySphereIntersection(
		const double origin[3],
		const double dir[3],
		const PanoramaBasis& basis,
		double& hit_u,
		double& hit_v)
	{
		const double a =
			dir[0] * dir[0]
			+ dir[1] * dir[1]
			+ dir[2] * dir[2];

		const double b =
			2.0 *
			(
				origin[0] * dir[0]
				+ origin[1] * dir[1]
				+ origin[2] * dir[2]
				);

		const double c =
			origin[0] * origin[0]
			+ origin[1] * origin[1]
			+ origin[2] * origin[2]
			- 1.0;

		const double disc =
			b * b - 4.0 * a * c;

		if (disc < 0.0)
			return false;

		const double root =
			std::sqrt(disc);

		const double t1 =
			(-b - root) / (2.0 * a);

		const double t2 =
			(-b + root) / (2.0 * a);

		const double t =
			(t1 > 1e-6)
			? t1
			: ((t2 > 1e-6) ? t2 : -1.0);

		if (t <= 1e-6)
			return false;

		const double hit[3] =
		{
			origin[0] + t * dir[0],
			origin[1] + t * dir[1],
			origin[2] + t * dir[2]
		};

		return PanoramaProcessor::directionToPanoramaUV(
			hit,
			basis,
			hit_u,
			hit_v);
	}

    // Bilinear sampling in scene-linear float; never quantize here.
    LinearRGB samplePanoramaBilinear(const HDRImage& pano, double u, double v) {
        if (pano.width <= 0 || pano.height <= 0) return LinearRGB();

        // Horizontal panorama wraps. Use width so u=1 wraps continuously to u=0.
        double px = u * static_cast<double>(pano.width);
        double py = v * static_cast<double>(pano.height - 1);
        int baseX = static_cast<int>(std::floor(px));
        int baseY = static_cast<int>(std::floor(py));
        double dx = px - std::floor(px);
        double dy = py - std::floor(py);

        int x0 = (baseX % pano.width + pano.width) % pano.width;
        int x1 = (x0 + 1) % pano.width;
        int y0 = std::max(0, std::min(baseY, pano.height - 1));
        int y1 = std::max(0, std::min(baseY + 1, pano.height - 1));

        const LinearRGB& c00 = pano.at(x0, y0);
        const LinearRGB& c10 = pano.at(x1, y0);
        const LinearRGB& c01 = pano.at(x0, y1);
        const LinearRGB& c11 = pano.at(x1, y1);

        float r = static_cast<float>((1.0 - dx) * (1.0 - dy) * c00.r + dx * (1.0 - dy) * c10.r +
                                     (1.0 - dx) * dy * c01.r + dx * dy * c11.r);
        float g = static_cast<float>((1.0 - dx) * (1.0 - dy) * c00.g + dx * (1.0 - dy) * c10.g +
                                     (1.0 - dx) * dy * c01.g + dx * dy * c11.g);
        float b = static_cast<float>((1.0 - dx) * (1.0 - dy) * c00.b + dx * (1.0 - dy) * c10.b +
                                     (1.0 - dx) * dy * c01.b + dx * dy * c11.b);
        return LinearRGB(r, g, b);
    }

} // namespace anonymous

// ----------------------------------------------------------------------
// PanoramaProcessor 静态方法实现
// ----------------------------------------------------------------------
bool PanoramaProcessor::loadImageLDR(const std::string& filename, HDRImage& img) {
#ifdef _WIN32
    int wlen = MultiByteToWideChar(CP_UTF8, 0, filename.c_str(), -1, nullptr, 0);
    if (wlen <= 0) return false;
    std::vector<wchar_t> wfilename(static_cast<size_t>(wlen));
    MultiByteToWideChar(CP_UTF8, 0, filename.c_str(), -1, wfilename.data(), wlen);
    FILE* file = _wfopen(wfilename.data(), L"rb");
#else
    FILE* file = fopen(filename.c_str(), "rb");
#endif
    if (!file) {
        std::cerr << "无法打开文件: " << filename << std::endl;
        return false;
    }

    int w = 0, h = 0, channels = 0;
    unsigned char* data = stbi_load_from_file(file, &w, &h, &channels, 3);
    fclose(file);
    if (!data) {
        std::cerr << "stb_image 解码失败: " << stbi_failure_reason() << std::endl;
        return false;
    }

    // Convert LDR sRGB to scene-linear [0,1], so interpolation is performed in linear space.
    img = HDRImage(w, h);
    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            const int idx = (y * w + x) * 3;
            float r = srgbToLinear(data[idx + 0] / 255.0f);
            float g = srgbToLinear(data[idx + 1] / 255.0f);
            float b = srgbToLinear(data[idx + 2] / 255.0f);
            img.at(x, y) = LinearRGB(r, g, b);
        }
    }
    stbi_image_free(data);
    printHDRStats("LDR loaded as linear", img);
    return true;
}

bool PanoramaProcessor::loadImageEXR(const std::string& filename, HDRImage& img) {
    if (loadEXR_Regular(filename, img)) return true;
    if (loadEXR_MultiPart(filename, img)) return true;
    if (loadEXR_Deep(filename, img)) return true;
    return false;
}

bool PanoramaProcessor::loadImage(const std::string& filename, HDRImage& img) {
    size_t dot = filename.find_last_of('.');
    if (dot == std::string::npos || dot + 1 >= filename.size()) return false;

    std::string ext = filename.substr(dot + 1);
    std::transform(ext.begin(), ext.end(), ext.begin(),
        [](unsigned char c) { return static_cast<char>(std::tolower(c)); });

    if (ext == "exr") return loadImageEXR(filename, img);
    if (ext == "hdr") return loadImageHDR(filename, img);
    if (ext == "jpg" || ext == "jpeg" || ext == "png" || ext == "bmp" || ext == "tga")
        return loadImageLDR(filename, img);

    std::cerr << "不支持的图像格式: ." << ext << std::endl;
    return false;
}

//bool PanoramaProcessor::raySphereIntersection(const double origin[3], const double dir[3],
//    double& hit_u, double& hit_v) {
//    return ::raySphereIntersection(origin, dir, hit_u, hit_v);
//}
bool PanoramaProcessor::raySphereIntersection(
	const double origin[3],
	const double dir[3],
	const PanoramaBasis& basis,
	double& hit_u,
	double& hit_v)
{
	return ::raySphereIntersection(
		origin,
		dir,
		basis,
		hit_u,
		hit_v);
}

std::vector<QPointF> PanoramaProcessor::computeCornerUVs(
    double cx, double cy, double cz,
    double yaw_deg, double pitch_deg, double roll_deg,
    double hfov_deg, double vfov_deg,
    int outW, int outH,
    //double northPanoramaDeg,
	const PanoramaBasis& basis,
    bool flipVertical)
{
    std::vector<QPointF> polygon;
    polygon.reserve(200);

    CameraTransform::RayContext rayCtx;
    if (!CameraTransform::buildRayContext(
            cx, cy, cz,
            yaw_deg, pitch_deg, roll_deg,
            hfov_deg, vfov_deg,
            outW, outH,
            flipVertical,
            rayCtx))
    {
        return polygon;
    }

    auto samplePoint = [&](double x, double y, QPointF& result) -> bool
    {
        double dir[3];
        if (!CameraTransform::pixelToENUDirection(rayCtx, x, y, dir))
            return false;

        double u = 0.0;
        double v = 0.0;
        //if (raySphereIntersection(rayCtx.originENU, dir, u, v))
        //{
        //    u = PanoramaProcessor::applyNorthPanoramaOffset(u, northPanoramaDeg);
        //    result = QPointF(u, v);
        //    return true;
        //}
		if (raySphereIntersection(
			rayCtx.originENU,
			dir,
			basis,
			u,
			v))
		{
			result = QPointF(u, v);
			return true;
		}
        return false;
    };

    const int N = 40;

    // Top edge.
    for (int i = 0; i <= N; ++i)
    {
        const double x = i * (outW - 1.0) / N;
        QPointF pt;
        if (samplePoint(x, 0.0, pt)) polygon.push_back(pt);
    }

    // Right edge.
    for (int i = 1; i <= N; ++i)
    {
        const double y = i * (outH - 1.0) / N;
        QPointF pt;
        if (samplePoint(outW - 1.0, y, pt)) polygon.push_back(pt);
    }

    // Bottom edge.
    for (int i = N - 1; i >= 0; --i)
    {
        const double x = i * (outW - 1.0) / N;
        QPointF pt;
        if (samplePoint(x, outH - 1.0, pt)) polygon.push_back(pt);
    }

    // Left edge.
    for (int i = N - 1; i >= 1; --i)
    {
        const double y = i * (outH - 1.0) / N;
        QPointF pt;
        if (samplePoint(0.0, y, pt)) polygon.push_back(pt);
    }

    if (polygon.size() < 3)
        polygon.clear();

    return polygon;
}

HDRImage PanoramaProcessor::perspectiveFromPanorama(const HDRImage& pano,
    double cx, double cy, double cz,
    double yaw_deg, double pitch_deg, double roll_deg,
    double hfov_deg, double vfov_deg,
    int outW, int outH, int aa,
	const PanoramaBasis& basis,
    //double northPanoramaDeg,
    bool flipVertical)
{
    HDRImage output(outW, outH);
    if (pano.width == 0 || pano.height == 0 || outW <= 0 || outH <= 0)
        return output;

    aa = std::max(1, aa);

    CameraTransform::RayContext rayCtx;
    if (!CameraTransform::buildRayContext(
            cx, cy, cz,
            yaw_deg, pitch_deg, roll_deg,
            hfov_deg, vfov_deg,
            outW, outH,
            flipVertical,
            rayCtx))
    {
        return output;
    }

    const double step = 1.0 / aa;

    for (int y = 0; y < outH; ++y)
    {
        for (int x = 0; x < outW; ++x)
        {
            double r_sum = 0.0;
            double g_sum = 0.0;
            double b_sum = 0.0;
            int valid = 0;

            for (int sy = 0; sy < aa; ++sy)
            {
                for (int sx = 0; sx < aa; ++sx)
                {
                    const double offX = (sx + 0.5) * step - 0.5;
                    const double offY = (sy + 0.5) * step - 0.5;

                    double dir[3];
                    if (!CameraTransform::pixelToENUDirection(
                            rayCtx,
                            x + offX,
                            y + offY,
                            dir))
                    {
                        continue;
                    }

                    double u = 0.0;
                    double v = 0.0;
					if (raySphereIntersection(
						rayCtx.originENU,
						dir,
						basis,
						u,
						v) &&
						v >= 0.0 && v <= 1.0)
					{
						const LinearRGB col =
							samplePanoramaBilinear(pano, u, v);
							r_sum += col.r;
							g_sum += col.g;
							b_sum += col.b;
							++valid;
					}
                    //if (raySphereIntersection(rayCtx.originENU, dir, u, v) &&
                    //    v >= 0.0 && v <= 1.0)
                    //{
                    //    u = PanoramaProcessor::applyNorthPanoramaOffset(u, northPanoramaDeg);
                    //    const LinearRGB col = samplePanoramaBilinear(pano, u, v);
                    //    r_sum += col.r;
                    //    g_sum += col.g;
                    //    b_sum += col.b;
                    //    ++valid;
                    //}
                }
            }

            if (valid > 0)
            {
                output.at(x, y) = LinearRGB(
                    static_cast<float>(r_sum / valid),
                    static_cast<float>(g_sum / valid),
                    static_cast<float>(b_sum / valid));
            }
            else
            {
                output.at(x, y) = LinearRGB();
            }
        }
    }

    return output;
}

Image PanoramaProcessor::toneMapForDisplay(const HDRImage& src, float exposure, float gamma) {
    Image out(src.width, src.height);
    if (src.width <= 0 || src.height <= 0 || src.data.empty()) return out;

    // Extended Reinhard white point. This keeps LDR white (~1.0 linear) white,
    // while compressing values far above 1 only for display.
    float whiteLum = 1.0f;
    for (size_t i = 0; i < src.data.size(); ++i) {
        const LinearRGB& p = src.data[i];
        if (!std::isfinite(p.r) || !std::isfinite(p.g) || !std::isfinite(p.b)) continue;
        float r = std::max(0.0f, p.r * exposure);
        float g = std::max(0.0f, p.g * exposure);
        float b = std::max(0.0f, p.b * exposure);
        float lum = 0.2126f * r + 0.7152f * g + 0.0722f * b;
        whiteLum = std::max(whiteLum, lum);
    }
    float white2 = std::max(1e-6f, whiteLum * whiteLum);

    for (int y = 0; y < src.height; ++y) {
        for (int x = 0; x < src.width; ++x) {
            const LinearRGB& p = src.at(x, y);
            float r = std::isfinite(p.r) ? std::max(0.0f, p.r * exposure) : 0.0f;
            float g = std::isfinite(p.g) ? std::max(0.0f, p.g * exposure) : 0.0f;
            float b = std::isfinite(p.b) ? std::max(0.0f, p.b * exposure) : 0.0f;

            float lum = 0.2126f * r + 0.7152f * g + 0.0722f * b;
            float mappedLum = 0.0f;
            if (lum > 1e-8f) {
                mappedLum = (lum * (1.0f + lum / white2)) / (1.0f + lum);
                float scale = mappedLum / lum;
                r *= scale; g *= scale; b *= scale;
            }

            r = linearToSRGB(r, gamma);
            g = linearToSRGB(g, gamma);
            b = linearToSRGB(b, gamma);

            auto to8 = [](float v) -> uint8_t {
                v = std::max(0.0f, std::min(1.0f, v));
                return static_cast<uint8_t>(v * 255.0f + 0.5f);
            };
            out.at(x, y) = sRGB(to8(r), to8(g), to8(b));
        }
    }
    return out;
}

bool PanoramaProcessor::loadImageHDR(const std::string& filename, HDRImage& img)
{
    FILE* file = nullptr;

#ifdef _WIN32
    int wlen = MultiByteToWideChar(CP_UTF8, 0, filename.c_str(), -1, nullptr, 0);
    if (wlen <= 0) {
        std::cerr << "HDR 文件路径转换失败: " << filename << std::endl;
        return false;
    }
    std::vector<wchar_t> wfilename(static_cast<size_t>(wlen));
    MultiByteToWideChar(CP_UTF8, 0, filename.c_str(), -1, wfilename.data(), wlen);
    file = _wfopen(wfilename.data(), L"rb");
#else
    file = fopen(filename.c_str(), "rb");
#endif

    if (!file) {
        std::cerr << "无法打开 HDR 文件: " << filename << std::endl;
        return false;
    }

    int width = 0, height = 0, channels = 0;
    // Radiance RGBE is decoded directly to linear float RGB.
    float* rgb = stbi_loadf_from_file(file, &width, &height, &channels, 3);
    fclose(file);

    if (!rgb) {
        std::cerr << "HDR 解码失败: " << stbi_failure_reason() << std::endl;
        return false;
    }
    if (width <= 0 || height <= 0) {
        stbi_image_free(rgb);
        std::cerr << "HDR 图像尺寸无效" << std::endl;
        return false;
    }

    img = HDRImage(width, height);
    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            const size_t idx = static_cast<size_t>(y) * static_cast<size_t>(width) + static_cast<size_t>(x);
            img.at(x, y) = LinearRGB(rgb[3 * idx + 0], rgb[3 * idx + 1], rgb[3 * idx + 2]);
        }
    }
    stbi_image_free(rgb);

    std::cout << "HDR 加载成功: " << width << "x" << height
              << ", source channels=" << channels << std::endl;
    printHDRStats("HDR loaded", img);
    return true;
}

void PanoramaProcessor::cameraToPanoramaXYZ(
	double cameraX,
	double cameraY,
	double cameraZ,
	double& panoX,
	double& panoY,
	double& panoZ)
{
	// Camera X -> Panorama Y
	// Camera Y -> Panorama Z
	// Camera Z -> Panorama X

	panoX = cameraZ;
	panoY = cameraX;
	panoZ = cameraY;
}
//double PanoramaProcessor::applyNorthPanoramaOffset(double worldU, double northPanoramaDeg)
//{
//    // northPanoramaDeg is the horizontal position of geographic North in the
//    // SOURCE panorama: 0 deg = left seam, 180 deg = image center, 360 deg = seam.
//    double northU = std::fmod(northPanoramaDeg, 360.0) / 360.0;
//    if (northU < 0.0) northU += 1.0;
//
//    // In the ENU world panorama convention North is worldU=0.5.
//    double sourceU = worldU + (northU - 0.5);
//    sourceU = std::fmod(sourceU, 1.0);
//    if (sourceU < 0.0) sourceU += 1.0;
//    return sourceU;
//}


bool PanoramaProcessor::makePanoramaBasisFromNorthUp(
	double nx, double ny, double nz,
	double ux, double uy, double uz,
	PanoramaBasis& basis)
{
	double n[3] = { nx, ny, nz };
	double u[3] = { ux, uy, uz };

	if (!normalize3(n) || !normalize3(u))
		return false;

	// Up 优先。
	// 从 North 中移除沿 Up 的分量。
	const double d = dot3(n, u);

	n[0] -= d * u[0];
	n[1] -= d * u[1];
	n[2] -= d * u[2];

	if (!normalize3(n))
		return false;

	// ENU-style right handed frame:
	//
	// E = N x U
	double e[3];
	cross3(n, u, e);

	if (!normalize3(e))
		return false;

	// 再算一次 N，消除输入误差。
	//
	// N = U x E
	cross3(u, e, n);

	if (!normalize3(n))
		return false;

	for (int i = 0; i < 3; ++i)
	{
		basis.east[i] = e[i];
		basis.north[i] = n[i];
		basis.up[i] = u[i];
	}

	return true;
}
bool PanoramaProcessor::directionToPanoramaUV(
	const double worldDir[3],
	const PanoramaBasis& basis,
	double& u,
	double& v)
{
	double d[3] =
	{
		worldDir[0],
		worldDir[1],
		worldDir[2]
	};

	if (!normalize3(d))
		return false;

	// World -> panorama local coordinate system.
	const double localEast = dot3(d, basis.east);
	const double localNorth = dot3(d, basis.north);

	double localUp = dot3(d, basis.up);

	localUp = std::max(
		-1.0,
		std::min(1.0, localUp));

	// Keep exactly the same equirectangular convention currently
	// used by envLightNoVtk.
	const double azimuth =
		std::atan2(localEast, localNorth);

	u = (azimuth + M_PI) / (2.0 * M_PI);

	if (u < 0.0)
		u += 1.0;

	if (u >= 1.0)
		u -= 1.0;

	v = std::acos(localUp) / M_PI;

	return true;
}


void PanoramaProcessor::panoramaUVToWorldDirection(
	double u,
	double v,
	const PanoramaBasis& basis,
	double worldDir[3])
{
	const double theta =
		v * M_PI;

	const double azimuth =
		u * 2.0 * M_PI - M_PI;

	const double horizontal =
		std::sin(theta);

	// Panorama-local ENU.
	const double localEast =
		horizontal * std::sin(azimuth);

	const double localNorth =
		horizontal * std::cos(azimuth);

	const double localUp =
		std::cos(theta);

	// panorama local -> ENU world
	for (int i = 0; i < 3; ++i)
	{
		worldDir[i] =
			localEast * basis.east[i]
			+ localNorth * basis.north[i]
			+ localUp * basis.up[i];
	}
}