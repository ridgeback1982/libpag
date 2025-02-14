#include "CVImageTool.h"
#include <iostream>

#if defined(__linux__)
#include "opencv2/opencv.hpp"


#endif


//zzy
namespace pag {

#if defined(__linux__)
bool CVImageTool::hasRectFrameInside(const std::string& path) {
    // 1. 读取图片
    cv::Mat img = cv::imread(path);
    if (img.empty()) {
        std::cerr << "Failed to load image!" << std::endl;
        return -1;
    }

    // 2. 灰度化
    cv::Mat gray;
    cv::cvtColor(img, gray, cv::COLOR_BGR2GRAY);

    // 3. 边缘检测
    cv::Mat edges;
    cv::Canny(gray, edges, 50, 150);

    // 4. 轮廓检测
    std::vector<std::vector<cv::Point>> contours;
    cv::findContours(edges, contours, cv::RETR_EXTERNAL, cv::CHAIN_APPROX_SIMPLE);

    // 5. 遍历所有轮廓，筛选出矩形
    for (const auto& contour : contours) {
        // 近似多边形
        std::vector<cv::Point> approx;
        cv::approxPolyDP(contour, approx, 0.02 * cv::arcLength(contour, true), true);

        // 如果多边形有 4 个顶点且是闭合的，认为是矩形
        if (approx.size() == 4 && cv::isContourConvex(approx)) {
            // 计算矩形的边界框
            cv::Rect boundingRect = cv::boundingRect(approx);

            // 检查是否接近图片中心
            cv::Point imageCenter(img.cols / 2, img.rows / 2);
            std::cout << "Find bounding reect, w:" << boundingRect.width << ", h:" << boundingRect.height
                << ", x:" << boundingRect.x << ", y:" << boundingRect.y << std::endl;
            if (boundingRect.contains(imageCenter)) {
                if (boundingRect.width > img.cols / 2 && boundingRect.height > img.rows / 2) {
                    std::cout << "Rectangle frame found at: "
                    << "x=" << boundingRect.x
                    << ", y=" << boundingRect.y
                    << ", width=" << boundingRect.width
                    << ", height=" << boundingRect.height << std::endl;
                    return true;
                }
            }
        }
    }

    return false;
}





#endif

}