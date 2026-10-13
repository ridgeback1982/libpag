#import "BookTitleDetector.h"

#if defined(__APPLE__) && defined(__MACH__)

#import <Vision/Vision.h>
#import <CoreGraphics/CoreGraphics.h>

#include <algorithm>

@interface VNRecognizedTextObservation (BookTitleDetector)
- (CGRect)boundingBoxForRange:(NSRange)range;
@end

static NSValue* BoxCGRect(CGRect r) {
    return [NSValue valueWithBytes:&r objCType:@encode(CGRect)];
}

static CGRect UnboxCGRect(NSValue* v) {
    CGRect r = CGRectZero;
    [v getValue:&r size:sizeof(CGRect)];
    return r;
}

@implementation BookTitleDetector

- (NSArray<NSValue *> *)detectBookTitleRegionsInPixelBuffer:(CVPixelBufferRef)pixelBuffer {
    if (pixelBuffer == NULL) {
        return @[];
    }

    const size_t width = CVPixelBufferGetWidth(pixelBuffer);
    const size_t height = CVPixelBufferGetHeight(pixelBuffer);
    if (width == 0 || height == 0) {
        return @[];
    }

    NSMutableArray<NSValue *> *result = [NSMutableArray array];

    @autoreleasepool {
        VNRecognizeTextRequest *request =
            [[VNRecognizeTextRequest alloc] initWithCompletionHandler:^(VNRequest *req, NSError *error) {
              if (error != nil || req.results == nil) {
                  return;
              }

              NSArray<VNRecognizedTextObservation *> *observations =
                  (NSArray<VNRecognizedTextObservation *> *)req.results;

              NSMutableArray<NSDictionary *> *allObs = [NSMutableArray array];
              for (VNRecognizedTextObservation *obs in observations) {
                  VNRecognizedText *top = [[obs topCandidates:1] firstObject];
                  if (top == nil || top.string.length == 0) {
                      continue;
                  }
                  if (top.confidence < 0.2f) {
                      continue;
                  }

                  CGRect bb = obs.boundingBox;
                  CGRect pixelRect = CGRectMake(bb.origin.x * width,
                                                (1.0 - bb.origin.y - bb.size.height) * height,
                                                bb.size.width * width,
                                                bb.size.height * height);

                  [allObs addObject:@{
                      @"text" : top.string,
                      @"rect" : BoxCGRect(pixelRect),
                      @"obs" : obs,
                  }];
              }

              [allObs sortUsingComparator:^NSComparisonResult(NSDictionary *a, NSDictionary *b) {
                CGRect ra = UnboxCGRect(a[@"rect"]);
                CGRect rb = UnboxCGRect(b[@"rect"]);
                CGFloat caY = CGRectGetMidY(ra);
                CGFloat cbY = CGRectGetMidY(rb);
                CGFloat lineTol = std::min(CGRectGetHeight(ra), CGRectGetHeight(rb)) * 0.5;
                if (fabs(caY - cbY) < lineTol) {
                    return CGRectGetMinX(ra) < CGRectGetMinX(rb) ? NSOrderedAscending
                                                                 : NSOrderedDescending;
                }
                return caY < cbY ? NSOrderedAscending : NSOrderedDescending;
              }];

              NSMutableIndexSet *used = [NSMutableIndexSet indexSet];
              NSInteger count = allObs.count;

              NSRegularExpression *regex =
                  [NSRegularExpression regularExpressionWithPattern:@"《[^》]*》|<[^>]*>"
                                                            options:0
                                                              error:nil];

              NSCharacterSet *leftSet = [NSCharacterSet characterSetWithCharactersInString:@"《<"];

              for (NSInteger i = 0; i < count; i++) {
                  if ([used containsIndex:i]) {
                      continue;
                  }

                  NSDictionary *entry = allObs[i];
                  NSString *text = entry[@"text"];
                  CGRect rect = UnboxCGRect(entry[@"rect"]);
                  VNRecognizedTextObservation *obs = entry[@"obs"];

                  NSArray<NSTextCheckingResult *> *matches =
                      [regex matchesInString:text options:0 range:NSMakeRange(0, text.length)];

                  if (matches.count > 0) {
                      for (NSTextCheckingResult *m in matches) {
                          CGRect box;
                          if ([obs respondsToSelector:@selector(boundingBoxForRange:)]) {
                              CGRect subBox = [obs boundingBoxForRange:m.range];
                              box = CGRectIsNull(subBox) || CGRectIsEmpty(subBox) ? obs.boundingBox : subBox;
                          } else {
                              box = obs.boundingBox;
                          }
                          CGRect pixelBox = CGRectMake(box.origin.x * width,
                                                       (1.0 - box.origin.y - box.size.height) * height,
                                                       box.size.width * width,
                                                       box.size.height * height);
                          pixelBox = [self inflateRect:pixelBox padding:4.0 maxW:width maxH:height];
                          [result addObject:BoxCGRect(pixelBox)];
                      }
                      [used addIndex:i];
                      continue;
                  }

                  NSRange leftRange = [text rangeOfCharacterFromSet:leftSet];
                  if (leftRange.location == NSNotFound) {
                      continue;
                  }

                  NSString *leftChar = [text substringWithRange:leftRange];
                  NSString *rightStr = [leftChar isEqualToString:@"<"] ? @">" : @"》";

                  CGRect mergedRect = rect;
                  BOOL foundRight = NO;
                  for (NSInteger j = i + 1; j < count; j++) {
                      if ([used containsIndex:j]) {
                          continue;
                      }
                      NSDictionary *entryJ = allObs[j];
                      CGRect rectJ = UnboxCGRect(entryJ[@"rect"]);
                      NSString *textJ = entryJ[@"text"];

                      CGFloat midIY = CGRectGetMidY(mergedRect);
                      CGFloat midJY = CGRectGetMidY(rectJ);
                      CGFloat lineTol = CGRectGetHeight(mergedRect) * 0.6;
                      if (fabs(midIY - midJY) > lineTol) {
                          break;
                      }

                      mergedRect = CGRectUnion(mergedRect, rectJ);

                      NSRange rightJ = [textJ rangeOfString:rightStr];
                      if (rightJ.location != NSNotFound) {
                          foundRight = YES;
                          [used addIndex:j];
                          break;
                      }
                  }

                  if (foundRight) {
                      mergedRect = [self inflateRect:mergedRect padding:4.0 maxW:width maxH:height];
                      [result addObject:BoxCGRect(mergedRect)];
                      [used addIndex:i];
                  }
              }
            }];

        request.recognitionLevel = VNRequestTextRecognitionLevelAccurate;
        request.usesLanguageCorrection = NO;
        request.recognitionLanguages = @[ @"zh-Hans", @"zh-Hant", @"en-US" ];
        if (@available(macOS 13.0, iOS 16.0, *)) {
            request.minimumTextHeight = 0.015;
        }

        VNImageRequestHandler *handler =
            [[VNImageRequestHandler alloc] initWithCVPixelBuffer:pixelBuffer options:@{}];
        NSError *err = nil;
        [handler performRequests:@[ request ] error:&err];
    }

    return result;
}

- (CGRect)inflateRect:(CGRect)rect padding:(CGFloat)padding maxW:(size_t)maxW maxH:(size_t)maxH {
    CGFloat x = std::max<CGFloat>(0, rect.origin.x - padding);
    CGFloat y = std::max<CGFloat>(0, rect.origin.y - padding);
    CGFloat w = std::min<CGFloat>((CGFloat)maxW - x, rect.size.width + padding * 2);
    CGFloat h = std::min<CGFloat>((CGFloat)maxH - y, rect.size.height + padding * 2);
    w = std::max<CGFloat>(0, w);
    h = std::max<CGFloat>(0, h);
    return CGRectMake(x, y, w, h);
}

@end

#endif
