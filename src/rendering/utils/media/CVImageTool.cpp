#include "CVImageTool.h"
#include <iostream>
#include <vector>
#include <algorithm>

#if defined(__linux__) || defined(__APPLE__) && defined(__MACH__)
#include "opencv2/opencv.hpp"
#endif


//zzy
namespace pag {

#if defined(__linux__) || defined(__APPLE__) && defined(__MACH__)
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
        cv::approxPolyDP(contour, approx, 0.05 * cv::arcLength(contour, true), true);

        // 如果多边形有 4 个顶点且是闭合的，认为是矩形
        if (approx.size() == 4 && cv::isContourConvex(approx)) {
            // 计算矩形的边界框
            cv::Rect boundingRect = cv::boundingRect(approx);

            // 检查是否接近图片中心
            cv::Point imageCenter(img.cols / 2, img.rows / 2);
            std::cout << "Find bounding rect, x:" << boundingRect.x << ", y:" << boundingRect.y 
                << ", w:" << boundingRect.width << ", h:" << boundingRect.height << std::endl;
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

// 将矩形顶点按顺时针排序
std::vector<cv::Point> arrangeRectanglePoints(const std::vector<cv::Point>& points) {
    // 计算质心
    cv::Point center(0, 0);
    for (const auto& p : points) {
        center.x += p.x;
        center.y += p.y;
    }
    center.x /= points.size();
    center.y /= points.size();
    
    // 根据与质心的夹角排序
    std::vector<cv::Point> sortedPoints = points;
    std::sort(sortedPoints.begin(), sortedPoints.end(), [center](const cv::Point& a, const cv::Point& b) {
        double angleA = atan2(a.y - center.y, a.x - center.x);
        double angleB = atan2(b.y - center.y, b.x - center.x);
        return angleA < angleB;
    });
    
    return sortedPoints;
}

std::vector<std::vector<cv::Point>> detectInnerApproxRectangles(const cv::Mat& image) {
    cv::Mat gray, binary, processed;
    
    // 预处理
    cv::cvtColor(image, gray, cv::COLOR_BGR2GRAY);
    cv::GaussianBlur(gray, gray, cv::Size(5, 5), 0);
    
    // 使用自适应阈值处理，更好地处理光照不均的情况
    cv::adaptiveThreshold(gray, binary, 255, cv::ADAPTIVE_THRESH_GAUSSIAN_C, 
                          cv::THRESH_BINARY_INV, 11, 2);
    
    // 形态学操作 - 可以帮助恢复遮挡或断开的部分
    cv::Mat kernel = cv::getStructuringElement(cv::MORPH_RECT, cv::Size(3, 3));
    cv::morphologyEx(binary, processed, cv::MORPH_CLOSE, kernel);
    
    // 寻找轮廓 - 使用RETR_LIST找到所有轮廓，不仅仅是外部轮廓
    std::vector<std::vector<cv::Point>> contours;
    cv::findContours(processed, contours, cv::RETR_LIST, cv::CHAIN_APPROX_SIMPLE);
    
    // 如果没有找到轮廓，返回空结果
    if (contours.empty()) return std::vector<std::vector<cv::Point>>();
    
    // 过滤和评估轮廓
    std::vector<std::vector<cv::Point>> rectangles;
    double imageArea = image.rows * image.cols;
    
    for (const auto& contour : contours) {
        // 过滤太小或太大的轮廓
        double contourArea = cv::contourArea(contour);
        if (contourArea < 0.001 * imageArea || contourArea > 0.95 * imageArea) continue;
        
        // 计算轮廓特征
        cv::RotatedRect minRect = cv::minAreaRect(contour);
        double rectArea = minRect.size.width * minRect.size.height;
        double areaRatio = contourArea / rectArea;
        
        // 多边形近似
        std::vector<cv::Point> approx;
        double epsilon = 0.02 * cv::arcLength(contour, true);
        cv::approxPolyDP(contour, approx, epsilon, true);
        
        // 评估矩形程度
        bool isRectangleLike = false;
        
        // 条件1: 近似后顶点数为4且为凸多边形
        if (approx.size() == 4 && cv::isContourConvex(approx)) {
            isRectangleLike = true;
        } 
        // 条件2: 面积比接近矩形(对圆角矩形有效)
        else if (areaRatio > 0.8) {
            isRectangleLike = true;
            // 使用最小面积矩形的顶点
            cv::Point2f vertices[4];
            minRect.points(vertices);
            approx.clear();
            for (int i = 0; i < 4; i++) {
                approx.push_back(cv::Point(vertices[i].x, vertices[i].y));
            }
        }
        
        if (isRectangleLike) {
            // 排序顶点，确保顺时针或逆时针
            rectangles.push_back(arrangeRectanglePoints(approx));
        }
    }
    
    return rectangles;
}

// 检测被遮挡的矩形
std::vector<std::vector<cv::Point>> detectOccludedRectangles(const cv::Mat& image) {
    cv::Mat gray, binary, processed;
    
    // 预处理
    cv::cvtColor(image, gray, cv::COLOR_BGR2GRAY);
    cv::GaussianBlur(gray, gray, cv::Size(5, 5), 0);
    
    // 使用自适应阈值，更好地处理不同光照条件
    cv::adaptiveThreshold(gray, binary, 255, cv::ADAPTIVE_THRESH_GAUSSIAN_C, 
                          cv::THRESH_BINARY_INV, 11, 2);
    
    // 形态学处理增强矩形特征
    cv::Mat kernel = cv::getStructuringElement(cv::MORPH_RECT, cv::Size(5, 5));
    cv::morphologyEx(binary, processed, cv::MORPH_CLOSE, kernel);
    
    // 寻找所有轮廓，包括内部轮廓
    std::vector<std::vector<cv::Point>> contours;
    cv::findContours(processed, contours, cv::RETR_LIST, cv::CHAIN_APPROX_SIMPLE);
    
    // 存储检测到的矩形
    std::vector<std::vector<cv::Point>> rectangles;
    double imageArea = image.rows * image.cols;
    
    // 过滤和分析每个轮廓
    for (const auto& contour : contours) {
        // 过滤太小或太大的轮廓
        double contourArea = cv::contourArea(contour);
        if (contourArea < 0.001 * imageArea || contourArea > 0.95 * imageArea) {
            continue;
        }
        
        // 计算轮廓特征
        cv::RotatedRect minRect = cv::minAreaRect(contour);
        double rectArea = minRect.size.width * minRect.size.height;
        double areaRatio = contourArea / rectArea;
        
        // 计算凸包
        std::vector<cv::Point> hull;
        cv::convexHull(contour, hull);
        double hullArea = cv::contourArea(hull);
        double hullToRectRatio = hullArea / rectArea;
        
        // 多边形近似
        std::vector<cv::Point> approx;
        double epsilon = 0.02 * cv::arcLength(contour, true);
        cv::approxPolyDP(contour, approx, epsilon, true);
        
        // 决策逻辑
        bool isRectangleLike = false;
        std::vector<cv::Point> rectPoints;
        
        // 情况1: 完整矩形 (四个顶点，凸多边形)
        if (approx.size() == 4 && cv::isContourConvex(approx)) {
            isRectangleLike = true;
            rectPoints = approx;
        }
        // 情况2: 圆角矩形 (面积比例接近矩形)
        else if (areaRatio > 0.8 && hullToRectRatio > 0.9) {
            isRectangleLike = true;
            // 使用最小面积矩形
            cv::Point2f vertices[4];
            minRect.points(vertices);
            for (int i = 0; i < 4; i++) {
                rectPoints.push_back(cv::Point(vertices[i].x, vertices[i].y));
            }
        }
        // 情况3: 被遮挡的矩形 (顶点数在3-5之间)
        else if (approx.size() >= 3 && approx.size() <= 5 && hullToRectRatio > 0.8) {
            isRectangleLike = true;
            // 使用最小面积矩形估计完整形状
            cv::Point2f vertices[4];
            minRect.points(vertices);
            for (int i = 0; i < 4; i++) {
                rectPoints.push_back(cv::Point(vertices[i].x, vertices[i].y));
            }
        }
        
        if (isRectangleLike && !rectPoints.empty()) {
            rectangles.push_back(arrangeRectanglePoints(rectPoints));
        }
    }
    
    // 合并重叠的矩形
    std::vector<std::vector<cv::Point>> mergedRectangles;
    std::vector<bool> used(rectangles.size(), false);
    
    for (size_t i = 0; i < rectangles.size(); i++) {
        if (used[i]) continue;
        
        cv::Rect rect1 = cv::boundingRect(rectangles[i]);
        
        for (size_t j = i + 1; j < rectangles.size(); j++) {
            if (used[j]) continue;
            
            cv::Rect rect2 = cv::boundingRect(rectangles[j]);
            cv::Rect intersection = rect1 & rect2;
            
            if (intersection.area() > 0.8 * std::min(rect1.area(), rect2.area())) {
                used[j] = true;
            }
        }
        
        mergedRectangles.push_back(rectangles[i]);
    }
    
    return mergedRectangles;
}


bool CVImageTool::hasApproxRectFrameInside(const std::string& path)
{
    // 1. 读取图片
    cv::Mat img = cv::imread(path);
    if (img.empty()) {
        std::cerr << "Failed to load image!" << std::endl;
        return -1;
    }

    //2. 模糊检测（圆角等）矩形
    auto vecApproxPoints = detectInnerApproxRectangles(img);
    for (const auto& points : vecApproxPoints) {
        // 如果多边形有 4 个顶点且是闭合的，认为是矩形
        if (points.size() == 4 && cv::isContourConvex(points)) {
            // 计算矩形的边界框
            cv::Rect boundingRect = cv::boundingRect(points);

            // 检查是否接近图片中心
            cv::Point imageCenter(img.cols / 2, img.rows / 2);
            std::cout << "Find bounding rect, x:" << boundingRect.x << ", y:" << boundingRect.y 
                << ", w:" << boundingRect.width << ", h:" << boundingRect.height << std::endl;
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

    //3. 遮挡检测矩形
    auto vecOccPoints = detectOccludedRectangles(img);
    for (const auto& points : vecOccPoints) {
        if (points.size() == 4 && cv::isContourConvex(points)) {
            // 计算矩形的边界框
            cv::Rect boundingRect = cv::boundingRect(points);
    
            // 检查是否接近图片中心
            cv::Point imageCenter(img.cols / 2, img.rows / 2);
            std::cout << "Find bounding rect, x:" << boundingRect.x << ", y:" << boundingRect.y 
                << ", w:" << boundingRect.width << ", h:" << boundingRect.height << std::endl;
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
