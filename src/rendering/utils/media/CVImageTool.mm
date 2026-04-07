#include "CVImageTool.h"

#if defined(__APPLE__) && defined(__MACH__)
#import <Foundation/Foundation.h>
#import <Vision/Vision.h>

#include <algorithm>
#include <cmath>
#include <iostream>
#include <vector>

#include "opencv2/opencv.hpp"

namespace pag {

static Rect unionRect(const Rect& a, const Rect& b) {
    return Rect::MakeLTRB(std::min(a.left, b.left), std::min(a.top, b.top), std::max(a.right, b.right),
                          std::max(a.bottom, b.bottom));
}

static float rectArea(const Rect& r) {
    return std::max(0.0f, r.right - r.left) * std::max(0.0f, r.bottom - r.top);
}

bool CVImageTool::hasRectFrameInside(const std::string& path) {
    cv::Mat img = cv::imread(path);
    if (img.empty()) {
        std::cerr << "Failed to load image!" << std::endl;
        return false;
    }

    cv::Mat gray;
    cv::cvtColor(img, gray, cv::COLOR_BGR2GRAY);

    cv::Mat edges;
    cv::Canny(gray, edges, 50, 150);

    std::vector<std::vector<cv::Point>> contours;
    cv::findContours(edges, contours, cv::RETR_EXTERNAL, cv::CHAIN_APPROX_SIMPLE);

    for (const auto& contour : contours) {
        std::vector<cv::Point> approx;
        cv::approxPolyDP(contour, approx, 0.05 * cv::arcLength(contour, true), true);
        if (approx.size() == 4 && cv::isContourConvex(approx)) {
            cv::Rect boundingRect = cv::boundingRect(approx);
            cv::Point imageCenter(img.cols / 2, img.rows / 2);
            if (boundingRect.contains(imageCenter)) {
                if (boundingRect.width > img.cols / 2 && boundingRect.height > img.rows / 2) {
                    return true;
                }
            }
        }
    }

    return false;
}

static std::vector<cv::Point> arrangeRectanglePoints(const std::vector<cv::Point>& points) {
    cv::Point center(0, 0);
    for (const auto& p : points) {
        center.x += p.x;
        center.y += p.y;
    }
    center.x /= static_cast<int>(points.size());
    center.y /= static_cast<int>(points.size());

    std::vector<cv::Point> sortedPoints = points;
    std::sort(sortedPoints.begin(), sortedPoints.end(),
              [center](const cv::Point& a, const cv::Point& b) {
                  double angleA = atan2(a.y - center.y, a.x - center.x);
                  double angleB = atan2(b.y - center.y, b.x - center.x);
                  return angleA < angleB;
              });
    return sortedPoints;
}

static std::vector<std::vector<cv::Point>> detectInnerApproxRectangles(const cv::Mat& image) {
    cv::Mat gray, binary, processed;
    cv::cvtColor(image, gray, cv::COLOR_BGR2GRAY);
    cv::GaussianBlur(gray, gray, cv::Size(5, 5), 0);
    cv::adaptiveThreshold(gray, binary, 255, cv::ADAPTIVE_THRESH_GAUSSIAN_C, cv::THRESH_BINARY_INV,
                          11, 2);

    cv::Mat kernel = cv::getStructuringElement(cv::MORPH_RECT, cv::Size(3, 3));
    cv::morphologyEx(binary, processed, cv::MORPH_CLOSE, kernel);

    std::vector<std::vector<cv::Point>> contours;
    cv::findContours(processed, contours, cv::RETR_LIST, cv::CHAIN_APPROX_SIMPLE);
    if (contours.empty()) {
        return {};
    }

    std::vector<std::vector<cv::Point>> rectangles;
    double imageArea = static_cast<double>(image.rows) * static_cast<double>(image.cols);

    for (const auto& contour : contours) {
        double contourArea = cv::contourArea(contour);
        if (contourArea < 0.001 * imageArea || contourArea > 0.95 * imageArea) {
            continue;
        }

        cv::RotatedRect minRect = cv::minAreaRect(contour);
        double rectArea = minRect.size.width * minRect.size.height;
        if (rectArea <= 0) {
            continue;
        }
        double areaRatio = contourArea / rectArea;

        std::vector<cv::Point> approx;
        double epsilon = 0.02 * cv::arcLength(contour, true);
        cv::approxPolyDP(contour, approx, epsilon, true);

        bool isRectangleLike = false;
        if (approx.size() == 4 && cv::isContourConvex(approx)) {
            isRectangleLike = true;
        } else if (areaRatio > 0.8) {
            isRectangleLike = true;
            cv::Point2f vertices[4];
            minRect.points(vertices);
            approx.clear();
            for (int i = 0; i < 4; i++) {
                approx.push_back(cv::Point(vertices[i].x, vertices[i].y));
            }
        }

        if (isRectangleLike) {
            rectangles.push_back(arrangeRectanglePoints(approx));
        }
    }

    return rectangles;
}

static std::vector<std::vector<cv::Point>> detectOccludedRectangles(const cv::Mat& image) {
    cv::Mat gray, binary, processed;
    cv::cvtColor(image, gray, cv::COLOR_BGR2GRAY);
    cv::GaussianBlur(gray, gray, cv::Size(5, 5), 0);
    cv::adaptiveThreshold(gray, binary, 255, cv::ADAPTIVE_THRESH_GAUSSIAN_C, cv::THRESH_BINARY_INV,
                          11, 2);

    cv::Mat kernel = cv::getStructuringElement(cv::MORPH_RECT, cv::Size(5, 5));
    cv::morphologyEx(binary, processed, cv::MORPH_CLOSE, kernel);

    std::vector<std::vector<cv::Point>> contours;
    cv::findContours(processed, contours, cv::RETR_LIST, cv::CHAIN_APPROX_SIMPLE);

    std::vector<std::vector<cv::Point>> rectangles;
    double imageArea = static_cast<double>(image.rows) * static_cast<double>(image.cols);

    for (const auto& contour : contours) {
        double contourArea = cv::contourArea(contour);
        if (contourArea < 0.001 * imageArea || contourArea > 0.95 * imageArea) {
            continue;
        }

        cv::RotatedRect minRect = cv::minAreaRect(contour);
        double rectArea = minRect.size.width * minRect.size.height;
        if (rectArea <= 0) {
            continue;
        }
        double areaRatio = contourArea / rectArea;

        std::vector<cv::Point> hull;
        cv::convexHull(contour, hull);
        double hullArea = cv::contourArea(hull);
        double hullToRectRatio = hullArea / rectArea;

        std::vector<cv::Point> approx;
        double epsilon = 0.02 * cv::arcLength(contour, true);
        cv::approxPolyDP(contour, approx, epsilon, true);

        bool isRectangleLike = false;
        std::vector<cv::Point> rectPoints;
        if (approx.size() == 4 && cv::isContourConvex(approx)) {
            isRectangleLike = true;
            rectPoints = approx;
        } else if (areaRatio > 0.8 && hullToRectRatio > 0.9) {
            isRectangleLike = true;
            cv::Point2f vertices[4];
            minRect.points(vertices);
            for (int i = 0; i < 4; i++) {
                rectPoints.push_back(cv::Point(vertices[i].x, vertices[i].y));
            }
        } else if (approx.size() >= 3 && approx.size() <= 5 && hullToRectRatio > 0.8) {
            isRectangleLike = true;
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

    std::vector<std::vector<cv::Point>> mergedRectangles;
    std::vector<bool> used(rectangles.size(), false);

    for (size_t i = 0; i < rectangles.size(); i++) {
        if (used[i]) {
            continue;
        }
        cv::Rect rect1 = cv::boundingRect(rectangles[i]);
        for (size_t j = i + 1; j < rectangles.size(); j++) {
            if (used[j]) {
                continue;
            }
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

bool CVImageTool::hasApproxRectFrameInside(const std::string& path) {
    cv::Mat img = cv::imread(path);
    if (img.empty()) {
        std::cerr << "Failed to load image!" << std::endl;
        return false;
    }

    auto vecApproxPoints = detectInnerApproxRectangles(img);
    for (const auto& points : vecApproxPoints) {
        if (points.size() == 4 && cv::isContourConvex(points)) {
            cv::Rect boundingRect = cv::boundingRect(points);
            cv::Point imageCenter(img.cols / 2, img.rows / 2);
            if (boundingRect.contains(imageCenter)) {
                if (boundingRect.width > img.cols / 2 && boundingRect.height > img.rows / 2) {
                    return true;
                }
            }
        }
    }

    auto vecOccPoints = detectOccludedRectangles(img);
    for (const auto& points : vecOccPoints) {
        if (points.size() == 4 && cv::isContourConvex(points)) {
            cv::Rect boundingRect = cv::boundingRect(points);
            cv::Point imageCenter(img.cols / 2, img.rows / 2);
            if (boundingRect.contains(imageCenter)) {
                if (boundingRect.width > img.cols / 2 && boundingRect.height > img.rows / 2) {
                    return true;
                }
            }
        }
    }

    return false;
}

static std::vector<Rect> runRecognizeText(CVPixelBufferRef pixelBuffer, int width, int height,
                                         CGRect regionOfInterest, float minConfidence,
                                         float minTextHeight, VNRequestTextRecognitionLevel level) {
    __block std::vector<Rect> rects;
    @autoreleasepool {
        VNRecognizeTextRequest* request = [[VNRecognizeTextRequest alloc]
            initWithCompletionHandler:^(VNRequest* req, NSError* error) {
                if (error != nil) {
                    return;
                }
                NSArray<VNRecognizedTextObservation*>* results =
                    (NSArray<VNRecognizedTextObservation*>*)req.results;
                if (results == nil) {
                    return;
                }
                for (VNRecognizedTextObservation* obs in results) {
                    VNRecognizedText* top = [[obs topCandidates:1] firstObject];
                    if (top == nil) {
                        continue;
                    }
                    if (top.string.length == 0) {
                        continue;
                    }
                    if (top.confidence < minConfidence) {
                        continue;
                    }

                    CGRect bb = obs.boundingBox;
                    const float x = static_cast<float>(bb.origin.x) * width;
                    const float y = (1.0f - static_cast<float>(bb.origin.y) - static_cast<float>(bb.size.height)) *
                                    height;
                    const float w = static_cast<float>(bb.size.width) * width;
                    const float h = static_cast<float>(bb.size.height) * height;
                    std::cout << "VNRecognizedText, x: " << x << ", y: " << y << ", w: " << w << ", h: " << h << ", confidence: " << top.confidence << std::endl;
                    rects.push_back(Rect::MakeLTRB(x, y, x + w, y + h));
                }
            }];
        request.recognitionLevel = level;
        request.usesLanguageCorrection = NO;
        request.recognitionLanguages = @[@"zh-Hans", @"en-US"];
        request.regionOfInterest = regionOfInterest;
        if (@available(macOS 13.0, iOS 16.0, *)) {
            request.minimumTextHeight = minTextHeight;
        }

        VNImageRequestHandler* handler = [[VNImageRequestHandler alloc] initWithCVPixelBuffer:pixelBuffer
                                                                                      options:@{}];
        NSError* err = nil;
        [handler performRequests:@[request] error:&err];
    }
    return rects;
}

static float iouOf(const Rect& a, const Rect& b) {
    const float left = std::max(a.left, b.left);
    const float top = std::max(a.top, b.top);
    const float right = std::min(a.right, b.right);
    const float bottom = std::min(a.bottom, b.bottom);
    const float w = std::max(0.0f, right - left);
    const float h = std::max(0.0f, bottom - top);
    const float inter = w * h;
    const float areaA = rectArea(a);
    const float areaB = rectArea(b);
    const float denom = areaA + areaB - inter + 1e-6f;
    return inter / denom;
}

static std::vector<Rect> mergeOverlappingRects(std::vector<Rect> rects, float iouThreshold) {
    if (rects.size() <= 1) {
        return rects;
    }
    std::sort(rects.begin(), rects.end(),
              [](const Rect& a, const Rect& b) { return rectArea(a) > rectArea(b); });

    std::vector<Rect> merged;
    merged.reserve(rects.size());
    for (const auto& r : rects) {
        bool didMerge = false;
        for (auto& m : merged) {
            if (iouOf(r, m) >= iouThreshold) {
                m = unionRect(m, r);
                didMerge = true;
                break;
            }
        }
        if (!didMerge) {
            merged.push_back(r);
        }
    }
    return merged;
}

std::vector<Rect> CVImageTool::detectSubtitleRegions(CVPixelBufferRef pixelBuffer) {
    if (pixelBuffer == nullptr) {
        return {};
    }
    const int width = static_cast<int>(CVPixelBufferGetWidth(pixelBuffer));
    const int height = static_cast<int>(CVPixelBufferGetHeight(pixelBuffer));
    if (width <= 0 || height <= 0) {
        return {};
    }

    auto candidates = runRecognizeText(pixelBuffer, width, height, CGRectMake(0, 0, 1, 1), 0.1f, 0.004f,
                                       VNRequestTextRecognitionLevelAccurate);
    candidates = mergeOverlappingRects(std::move(candidates), 0.35f);
    return candidates;
}

}  // namespace pag

#endif
