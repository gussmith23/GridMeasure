#include <jni.h>
#include <string>
#include <vector>
#include <iterator>
#include <fstream>
#include "opencv2/aruco/charuco.hpp"
#include "opencv2/aruco/dictionary.hpp"
#include "opencv2/imgproc.hpp"
#include "opencv2/highgui.hpp"
#include "opencv2/calib3d.hpp"

cv::Mat cameraMatrix, distCoeffs, newCameraMatrix;
double repError;

// Debug
cv::Mat currentImage;

std::string fileStoragePath;

bool calibrated = false;

// TODO Still not the best way to do this, but better.
// We should probably allow the charuco card to be configured on the Java end - but right now it's
// unimportant - we're just hard-coding the charuco card using this function.
// It may be overkill anyway to allow configuration of charuco card on the Java end - what practical
// use does that have?
const cv::Ptr<cv::aruco::CharucoBoard> getBoard() {
    const cv::Ptr<cv::aruco::Dictionary> dictionary = cv::aruco::getPredefinedDictionary(cv::aruco::DICT_7X7_1000);
    return cv::aruco::CharucoBoard::create(5, 7, 1.0f, 0.5f, dictionary);
}

bool fileExists(std::string filename)
{
    std::ifstream file(filename);
    return file.good();
}

extern "C"
void Java_edu_psu_armstrong1_gridmeasure_ComputerVisionUtils_init(
    JNIEnv* env,
    jobject,
    jstring fileStoragePathJstring)
{

    fileStoragePath = std::string(env->GetStringUTFChars(fileStoragePathJstring, JNI_FALSE)) + "/";

    if(fileExists(fileStoragePath + "calibration.xml")) {
        cv::FileStorage::FileStorage calibrationFile(fileStoragePath + "calibration.xml", cv::FileStorage::READ);
        calibrationFile["cameraMatrix"] >> cameraMatrix;
        calibrationFile["distCoeffs"] >> distCoeffs;
    } else {
        cameraMatrix = cv::Mat::eye(3, 3, CV_64F);
        distCoeffs = cv::Mat::zeros(8, 1, CV_64F);  // todo is this right?
    }
}


extern "C"
void Java_edu_psu_armstrong1_gridmeasure_ComputerVisionUtils_undistort(
        JNIEnv* env,
        jobject /* this */,
        jlong inMat,
        jlong outMat) {

    cv::Mat* pInMat = (cv::Mat*)inMat;
    cv::Mat* pOutMat = (cv::Mat*)outMat;

    newCameraMatrix = cv::getOptimalNewCameraMatrix(cameraMatrix, distCoeffs, pInMat->size(), 1.0);

    cv::undistort(*pInMat, *pOutMat, cameraMatrix, distCoeffs, newCameraMatrix);

    // Debug
    currentImage = pOutMat->clone();
}

/**
 *
 */
cv::Point2f imagePointToWorldPoint(
        cv::Point2f imagePoint,
        cv::Mat rvec,
        cv::Mat tvec)
{
    cv::Mat rotationMatrix;
    cv::Rodrigues(rvec,rotationMatrix);

    cv::Mat uvPoint = cv::Mat::ones(3,1,cv::DataType<double>::type); //u,v,1
    uvPoint.at<double>(0,0) = (double)imagePoint.x;
    uvPoint.at<double>(1,0) = (double)imagePoint.y;

    cv::Mat tempMat, tempMat2;
    double s;
    tempMat = rotationMatrix.inv() * newCameraMatrix.inv() * uvPoint;
    tempMat2 = rotationMatrix.inv() * tvec;
    s = 0 + tempMat2.at<double>(2,0); // we can set z to whatever; here we set it to 0.
    s /= tempMat.at<double>(2,0);

    cv::Mat point =  rotationMatrix.inv() * (s * newCameraMatrix.inv() * uvPoint - tvec);
    return cv::Point2f(point.at<double>(0,0), point.at<double>(1,0));
}

/**
 * Image must be grayscale.
 */
bool findCharuco(
        cv::Mat in,
        const cv::Ptr<cv::aruco::CharucoBoard> board,
        std::vector<cv::Point2f>& charucoCorners,
        std::vector<int>& charucoIds,
        cv::InputArray _cameraMatrix = cv::noArray(),
        cv::InputArray _distCoeffs = cv::noArray())
{
    std::vector< int > markerIds;
    std::vector< std::vector<cv::Point2f> > markerCorners;
    cv::aruco::detectMarkers(in, board->dictionary, markerCorners, markerIds);

    // if at least one marker detected
    if(markerIds.size() > 0) {
        cv::aruco::interpolateCornersCharuco(markerCorners, markerIds, in, board, charucoCorners, charucoIds, _cameraMatrix, _distCoeffs);
        return true;
    }
    return false;
}

/**
 * Images must be grayscale
 */
bool calibrateWithCharuco(
        cv::Ptr<cv::aruco::CharucoBoard> board,
        const std::vector<cv::Mat>& images,
        cv::Size imgSize)
{
    std::vector< std::vector<cv::Point2f> > allCharucoCorners;
    std::vector< std::vector<int> > allCharucoIds;

    for (cv::Mat image : images)
    {
        std::vector<cv::Point2f> charucoCorners;
        std::vector<int> charucoIds;

        if (findCharuco(image, board, charucoCorners, charucoIds))
        {
            allCharucoCorners.push_back(charucoCorners);
            allCharucoIds.push_back(charucoIds);
        }
    }


    int calibrationFlags = 0;
    std::vector<cv::Mat> rvecs_unused, tvecs_unused;
    repError = cv::aruco::calibrateCameraCharuco(allCharucoCorners, allCharucoIds, board, imgSize, cameraMatrix, distCoeffs, rvecs_unused, tvecs_unused, calibrationFlags);

    // Save params.
    cv::FileStorage::FileStorage calibrationFile(fileStoragePath + "calibration.xml", cv::FileStorage::WRITE);
    calibrationFile << "cameraMatrix" << cameraMatrix << "distCoeffs" << distCoeffs;
    calibrationFile.release();

    // Mark as calibrated
    calibrated = true;

    return true;
}

/**
 *  outlinePointsJfloatArray will be an array of floats. Take them two at a time to get the x,y pairs.
 *  Clearly, this means outlinePointsJfloatArray should be of even length.
 */
extern "C"
jfloatArray Java_edu_psu_armstrong1_gridmeasure_ComputerVisionUtils_measurementsFromOutlineNative(
    JNIEnv* env,
    jobject,
    jobject imageJobject,
    jfloatArray outlinePointsJfloatArray)
{
    jfloatArray err = env->NewFloatArray(0);

    // Get the float array.
    jfloat* jfloatArr = env->GetFloatArrayElements(outlinePointsJfloatArray, 0);

    jclass matclass = env->FindClass("org/opencv/core/Mat");
    jmethodID getPtrMethod = env->GetMethodID(matclass, "getNativeObjAddr", "()J");

    cv::Mat image = *(cv::Mat*)env->CallLongMethod(imageJobject, getPtrMethod);

    // Undistort
    //cv::Mat undistortedImage;
    //cv::undistort(image, undistortedImage, cameraMatrix, distCoeffs, newCameraMatrix);
    ///image = undistortedImage;

    // Debug - just use the image from the last time the native distort function was called.
    image = currentImage.clone();
    cv::cvtColor(image, image, CV_RGBA2GRAY);

    // New distortion coeffs - assume distortion 0.
    cv::Mat newDistCoeffs = cv::Mat::zeros(8, 1, CV_64F);  // todo is this right?

    const cv::Ptr<cv::aruco::CharucoBoard> board = getBoard();

    //estimatePoseCharucoBoard
    std::vector<cv::Point2f> charucoCorners;
    std::vector<int> charucoIds;
    // todo handle error
    if (!findCharuco(image, board, charucoCorners, charucoIds, newCameraMatrix, newDistCoeffs)) return err;

    cv::Mat rvec,tvec;
    // todo handle error
    if (!cv::aruco::estimatePoseCharucoBoard(charucoCorners, charucoIds, board, newCameraMatrix, newDistCoeffs, rvec, tvec)) return err;


    // Create homography

    std::vector<cv::Point2f> obj;
    std::vector<cv::Point2f> scene;
    for (int i = 0; i < charucoCorners.size(); i++) {
        cv::Point2f corner = charucoCorners.at(i);
        int id = charucoIds.at(i);
        scene.push_back(cv::Point2f(board->chessboardCorners[id].x, board->chessboardCorners[id].y));
        obj.push_back(corner);
    }

    cv::Mat H = findHomography( obj, scene, CV_RANSAC );

    std::vector<cv::Point2f> obj_corners;
    std::vector<cv::Point2f> scene_corners;
    for (int i = 0; i < env->GetArrayLength(outlinePointsJfloatArray); i += 2)
    {
        obj_corners.push_back(cv::Point2f(jfloatArr[i], jfloatArr[i+1]));
    }

    cv::perspectiveTransform( obj_corners, scene_corners, H);

    float* outPoints = new float[scene_corners.size()*2];
    // Note that we jump in increments of two.
    // Todo - should we check that the length of the array is even?
    for (int i = 0; i < scene_corners.size(); i++)
    {
        outPoints[2*i] = scene_corners.at(i).x;
        outPoints[2*i+1] = scene_corners.at(i).y;
    }

    jfloatArray out = env->NewFloatArray(scene_corners.size()*2);
    env->SetFloatArrayRegion(out, 0, scene_corners.size()*2, outPoints);

    delete [] outPoints;


/*
    float* outPoints = new float[env->GetArrayLength(outlinePointsJfloatArray)];
    // Note that we jump in increments of two.
    // Todo - should we check that the length of the array is even?
    for (int i = 0; i < env->GetArrayLength(outlinePointsJfloatArray); i += 2)
    {
        cv::Point2f worldPoint = imagePointToWorldPoint(cv::Point2f(cv::Point2f(jfloatArr[i], jfloatArr[i+1])), rvec, tvec); // todo: why is it constructing a Point2f twice
        outPoints[i] = worldPoint.x;
        outPoints[i+1] = worldPoint.y;
    }

    jfloatArray out = env->NewFloatArray(env->GetArrayLength(outlinePointsJfloatArray));
    env->SetFloatArrayRegion(out, 0, env->GetArrayLength(outlinePointsJfloatArray), outPoints);

    delete [] outPoints;*/

    return out;
}

extern "C"
void Java_edu_psu_armstrong1_gridmeasure_ComputerVisionUtils_calibrateWithCharucoNative(
    JNIEnv* env,
    jobject,
    jobjectArray imagePathsArray)
{
    // TODO this shouldn't go here
    const cv::Ptr<cv::aruco::CharucoBoard> board = getBoard();

    std::vector<cv::Mat> images;

    cv::Size imgSize;

    for (int i = 0; i < env->GetArrayLength(imagePathsArray); i++)
    {
        // Get path
        jstring pathJstring = (jstring) (env->GetObjectArrayElement(imagePathsArray, i));
        const char *path = env->GetStringUTFChars(pathJstring, 0);

        cv::Mat image = cv::imread(path, CV_LOAD_IMAGE_GRAYSCALE);
        if (image.data != NULL)
        {
            images.push_back(image);
            if (imgSize.height == 0 && imgSize.width == 0) imgSize = image.size();
            else {
                // TODO we should handle an error here.
            }
        }
    }

    calibrateWithCharuco(board, images, imgSize);
}

extern "C"
void Java_edu_psu_armstrong1_gridmeasure_ComputerVisionUtils_calibrateWithCharucoMatsNative(
    JNIEnv* env,
    jobject,
    jobjectArray imageArray)
{
    const cv::Ptr<cv::aruco::CharucoBoard> board = getBoard();

    std::vector<cv::Mat> images;

    jclass matclass = env->FindClass("org/opencv/core/Mat");
    jmethodID getPtrMethod = env->GetMethodID(matclass, "getNativeObjAddr", "()J");

    cv::Size imgSize;

    for (int i = 0; i < env->GetArrayLength(imageArray); i++)
    {
        cv::Mat image = *(cv::Mat*)env->CallLongMethod(env->GetObjectArrayElement(imageArray, i), getPtrMethod);

        if (image.data != NULL)
        {
            images.push_back(image);
            if (imgSize.height == 0 && imgSize.width == 0) imgSize = image.size();
            else {
                // TODO we should handle an error here.
            }
        }
    }

    calibrateWithCharuco(board, images, imgSize);
}


extern "C"
jstring
Java_edu_psu_armstrong1_gridmeasure_ComputerVisionUtils_stringFromJNI(
        JNIEnv* env,
        jobject /* this */,
        jlong inMat,
        jlong outMat) {

    cv::Mat* pInMat = (cv::Mat*)inMat;
    cv::Mat* pOutMat = (cv::Mat*)outMat;

    cv::cvtColor(*pInMat, *pInMat, CV_RGB2GRAY);
    cv::resize(*pInMat, *pInMat, cv::Size(0,0), .1f, .1f, cv::INTER_NEAREST);

    // I'm getting errors if i try to draw the detected corners on the out mat if the out mat
    // is copied from in mat before in mat is turned to gray.
    *pOutMat = pInMat->clone();

    const cv::Ptr<cv::aruco::CharucoBoard> board = getBoard();

    std::vector<cv::Point2f> charucoCorners;
    std::vector<int> charucoIds;
    if (findCharuco(*pInMat, board, charucoCorners, charucoIds)) {
        cv::aruco::drawDetectedCornersCharuco(*pOutMat, charucoCorners, charucoIds, cv::Scalar(255,255,255));
    }

    std::string hello = "hello";
    return env->NewStringUTF(hello.c_str());
}

extern "C"
void
Java_edu_psu_armstrong1_gridmeasure_ComputerVisionUtils_drawAxis(
        JNIEnv* env,
        jobject /* this */,
        jlong inMat,
        jlong outMat) {

    cv::Mat* pInMat = (cv::Mat*)inMat;
    cv::Mat* pOutMat = (cv::Mat*)outMat;

    cv::cvtColor(*pInMat, *pInMat, CV_BGRA2BGR);


    *pOutMat = *pInMat;

    //estimatePoseCharucoBoard
    std::vector<cv::Point2f> charucoCorners;
    std::vector<int> charucoIds;

    // todo handle error
    if (!findCharuco(*pInMat, getBoard(), charucoCorners, charucoIds)) return;

    cv::Mat rvec,tvec;
    // todo handle error
    if (!cv::aruco::estimatePoseCharucoBoard(charucoCorners, charucoIds, getBoard(), cameraMatrix, distCoeffs, rvec, tvec)) return;

    cv::aruco::drawAxis(*pOutMat, cameraMatrix, distCoeffs, rvec, tvec, 5.0);
}