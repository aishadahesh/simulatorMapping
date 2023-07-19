//
// Created by Liam Vanunu
//

#include <unistd.h>
#include <nlohmann/json.hpp>
#include <opencv2/opencv.hpp>
#include <pangolin/pangolin.h>

#include "System.h"
#include "include/OfflineMapPoint.h"

#ifndef ORB_SLAM2_SIMULATOR_H
#define ORB_SLAM2_SIMULATOR_H

class Simulator {
public:
    // Methods
    Simulator();
    ~Simulator();

    void Run();

    void ToggleFollowCamera();
    void ToggleShowPoints();
    void DoReset();
    void MoveLeft();
    void MoveRight();
    void MoveDown();
    void MoveUp();
    void MoveBackward();
    void MoveForward();
    void RotateLeft();
    void RotateRight();
    void RotateDown();
    void RotateUp();
    void FinishScan();

    std::vector<OfflineMapPoint*> GetCloudPoint();

    void SetResultPoint(const cv::Point3d resultPoint);
    void CheckResults();

private:
    // Methods
    void initPoints();

    void createSimulatorSettings();

    void build_window(std::string title);

    void trackOrbSlam();

    std::vector<OfflineMapPoint*> getPointsFromTcw();

    void reset();
    void applyUpToModelCam(double value);
    void applyRightToModelCam(double value);
    void applyForwardToModelCam(double value);

    void applyYawRotationToModelCam(double value);
    void applyPitchRotationToModelCam(double value);

    void drawMapPoints();
    void drawResultPoints();

    void updateTwcByResultPoint();

    void saveOnlyNewPoints();

    void BuildCloudScanned();

    // Members
    nlohmann::json mData;

    std::vector<OfflineMapPoint*> mPoints;

    bool mUseOrbSlam;
    std::string mVocPath;
    bool mTrackImages;
    bool mLoadMap;
    std::string mLoadMapPath;
    ORB_SLAM2::System *mSystem;

    std::vector<OfflineMapPoint*> mCurrentFramePoints;
    std::vector<OfflineMapPoint*> mPointsSeen;
    std::vector<OfflineMapPoint*> mNewPointsSeen;

    std::vector<OfflineMapPoint*> mCloudScanned;

    cv::Point3d mResultPoint;
    cv::Point3d mRealResultPoint;

    std::string mSimulatorViewerTitle;
    std::string mResultsWindowTitle;

    double mPointSize;
    double mResultsPointSize;

    double mRotateScale;
    double mMovingScale;

    pangolin::OpenGlRenderState mS_cam;

    pangolin::View mD_cam;

    pangolin::OpenGlMatrix mTcw;
    pangolin::OpenGlMatrix mTwc;    

    bool mFollow;

    bool mFollowCamera;
    bool mShowPoints;
    bool mReset;
    bool mMoveLeft;
    bool mMoveRight;
    bool mMoveDown;
    bool mMoveUp;
    bool mMoveBackward;
    bool mMoveForward;
    bool mRotateLeft;
    bool mRotateRight;
    bool mRotateDown;
    bool mRotateUp;
    bool mFinishScan;

    float mViewpointX;
    float mViewpointY;
    float mViewpointZ;
    float mViewpointF;

    cv::Point3d mStartPosition;
    cv::Point3d mCurrentPosition;

    double mStartYaw;
    double mCurrentYaw;
    double mStartPitch;
    double mCurrentPitch;
    double mStartRoll;
    double mCurrentRoll;

    std::string mSimulatorPath;
    std::string mCloudPointPath;
    std::string mConfigPath;

    bool mCloseResults;
};

#endif //ORB_SLAM2_SIMULATOR_H