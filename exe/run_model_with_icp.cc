// Memory leak check
// No memory leak was founded

#include <thread>
#include <future>
#include <queue>

#include <pangolin/pangolin.h>
#include <pangolin/geometry/geometry.h>
#include <pangolin/gl/glsl.h>
#include <pangolin/gl/glvbo.h>

#include <pangolin/utils/file_utils.h>
#include <pangolin/geometry/glgeometry.h>

#include "include/run_model/TextureShader.h"
#include "include/Auxiliary.h"

#include <Eigen/SVD>
#include <Eigen/Geometry>

#include <opencv2/core.hpp>
#include <opencv2/opencv.hpp>
#include <opencv2/calib3d.hpp>

#define NEAR_PLANE 0.1
#define FAR_PLANE 20

void drawPoints(std::vector<cv::Point3d> seen_points, std::vector<cv::Point3d> new_points_seen) {
    std::string settingPath = Auxiliary::GetGeneralSettingsPath();
    std::ifstream programData(settingPath);
    nlohmann::json data;
    programData >> data;
    programData.close();

    const int point_size = data["pointSize"];

    glPointSize(point_size);
    glBegin(GL_POINTS);
    glColor3f(0.0, 0.0, 0.0);

    for (auto point: seen_points) {
        glVertex3f((float) (point.x), (float) (point.y), (float) (point.z));
    }
    glEnd();

    glPointSize(point_size);
    glBegin(GL_POINTS);
    glColor3f(1.0, 0.0, 0.0);

    for (auto point: new_points_seen) {
        glVertex3f((float) (point.x), (float) (point.y), (float) (point.z));
    }
    std::cout << new_points_seen.size() << std::endl;

    glEnd();
}

Eigen::Matrix4f loadMatrixFromFile(const std::string &filename) {
    Eigen::Matrix4f matrix;
    std::ifstream infile(filename);

    if (infile.is_open()) {
        int row = 0;
        std::string line;
        while (std::getline(infile, line)) {
            std::istringstream ss(line);
            std::string value;
            int col = 0;
            while (std::getline(ss, value, ',')) {
                matrix(row, col) = std::stof(value);
                col++;
            }
            row++;
        }
        infile.close();
    } else {
        std::cerr << "Cannot open file: " << filename << std::endl;
    }

    return matrix;
}

Eigen::Matrix4f openGlMatrixToEigen(const pangolin::OpenGlMatrix &m) {
    Eigen::Matrix4f eigen_matrix;
    for (int row = 0; row < 4; ++row) {
        for (int col = 0; col < 4; ++col) {
            eigen_matrix(row, col) = m(row, col);
        }
    }
    return eigen_matrix;
}

Eigen::Vector4f inverseTransformPoint(const Eigen::Vector4f &point, const Eigen::Matrix4f &transformation) {
    Eigen::Matrix4f inverse_transformation = transformation.inverse();
    return inverse_transformation * point;
}

std::vector<cv::Point3d> convert_points(std::vector<cv::Point3d>& points, Eigen::Matrix4f& transformation_mat) {
    std::vector<cv::Point3d> transformed_points;
    for (auto& point : points) {
        Eigen::Vector4f transformed_point = transformation_mat * Eigen::Vector4f((float)point.x, (float)point.y, (float)point.z, 1.0f);
        transformed_points.emplace_back(cv::Point3d((double)transformed_point[0], (double)transformed_point[1], (double)transformed_point[2]));
    }
    return transformed_points;
}

int main(int argc, char **argv) {
    std::string settingPath = Auxiliary::GetGeneralSettingsPath();
    std::ifstream programData(settingPath);
    nlohmann::json data;
    programData >> data;
    programData.close();

    std::string configPath = data["DroneYamlPathSlam"];
    cv::FileStorage fSettings(configPath, cv::FileStorage::READ);

    float fx = fSettings["Camera.fx"];
    float fy = fSettings["Camera.fy"];
    float cx = fSettings["Camera.cx"];
    float cy = fSettings["Camera.cy"];
    float viewpointX = fSettings["RunModel.ViewpointX"];
    float viewpointY = fSettings["RunModel.ViewpointY"];
    float viewpointZ = fSettings["RunModel.ViewpointZ"];

    Eigen::Matrix3d K;
    K << fx, 0.0, cx, 0.0, fy, cy, 0.0, 0.0, 1.0;
    cv::Mat K_cv = (cv::Mat_<float>(3, 3) << fx, 0.0, cx, 0.0, fy, cy, 0.0, 0.0, 1.0);
    Eigen::Vector2i viewport_desired_size(640, 480);

    cv::Mat img;

    // Options
    bool show_bounds = false;
    bool show_axis = false;
    bool show_x0 = false;
    bool show_y0 = false;
    bool show_z0 = false;
    bool cull_backfaces = false;

    // Create Window for rendering
    pangolin::CreateWindowAndBind("Main", viewport_desired_size[0], viewport_desired_size[1]);
    glEnable(GL_DEPTH_TEST);

    // Define Projection and initial ModelView matrix
    pangolin::OpenGlRenderState s_cam(
            pangolin::ProjectionMatrix(viewport_desired_size(0), viewport_desired_size(1), K(0, 0), K(1, 1), K(0, 2), K(1, 2), NEAR_PLANE, FAR_PLANE),
            pangolin::ModelViewLookAt(viewpointX, viewpointY, viewpointZ, 0, 0, 0, 0.0, -1.0, pangolin::AxisY)
    );

    // Create Interactive View in window
    pangolin::Handler3D handler(s_cam);
    pangolin::View &d_cam = pangolin::CreateDisplay()
            .SetBounds(0.0, 1.0, 0.0, 1.0, ((float)-viewport_desired_size[0] / (float)viewport_desired_size[1]))
            .SetHandler(&handler);

    // Load Geometry asynchronously
    std::string model_path = data["modelPath"];
    const pangolin::Geometry geom_to_load = pangolin::LoadGeometry(model_path);
    auto aabb = pangolin::GetAxisAlignedBox(geom_to_load);
    Eigen::AlignedBox3f total_aabb;
    total_aabb.extend(aabb);
    const auto mvm = pangolin::ModelViewLookAt(viewpointX, viewpointY, viewpointZ, 0, 0, 0, 0.0, -1.0, pangolin::AxisY);
    const auto proj = pangolin::ProjectionMatrix(viewport_desired_size(0), viewport_desired_size(1), K(0, 0), K(1, 1), K(0, 2), K(1, 2), NEAR_PLANE, FAR_PLANE);
    s_cam.SetModelViewMatrix(mvm);
    s_cam.SetProjectionMatrix(proj);
    const pangolin::GlGeometry geomToRender = pangolin::ToGlGeometry(geom_to_load);
    // Render tree for holding object position
    pangolin::GlSlProgram default_prog;
    auto LoadProgram = [&]() {
        default_prog.ClearShaders();
        default_prog.AddShader(pangolin::GlSlAnnotatedShader, pangolin::shader);
        default_prog.Link();
    };
    LoadProgram();
    pangolin::RegisterKeyPressCallback('b', [&]() { show_bounds = !show_bounds; });
    pangolin::RegisterKeyPressCallback('0', [&]() { cull_backfaces = !cull_backfaces; });

    // Show axis and axis planes
    pangolin::RegisterKeyPressCallback('a', [&]() { show_axis = !show_axis; });
    pangolin::RegisterKeyPressCallback('x', [&]() { show_x0 = !show_x0; });
    pangolin::RegisterKeyPressCallback('y', [&]() { show_y0 = !show_y0; });
    pangolin::RegisterKeyPressCallback('z', [&]() { show_z0 = !show_z0; });

    cv::Mat Twc;
    bool use_lab_icp = bool(data["useLabICP"]);
    std::cout << use_lab_icp << std::endl;

    std::string transformation_matrix_csv_path;
    if (use_lab_icp)
    {
        transformation_matrix_csv_path = std::string(data["framesOutput"]) + "frames_lab_transformation_matrix.csv";
    }
    else
    {
        transformation_matrix_csv_path = std::string(data["framesOutput"]) + "frames_transformation_matrix.csv";
    }
    Eigen::Matrix4f transformation = loadMatrixFromFile(transformation_matrix_csv_path);
    std::cout << transformation << std::endl;

    Eigen::Vector3d Pick_w = handler.Selected_P_w();
    std::vector<Eigen::Vector3d> Picks_w;

    while (!pangolin::ShouldQuit()) {
        if ((handler.Selected_P_w() - Pick_w).norm() > 1E-6) {
            Pick_w = handler.Selected_P_w();
            Picks_w.push_back(Pick_w);
            std::cout << pangolin::FormatString("\"Translation\": [%,%,%]", Pick_w[0], Pick_w[1], Pick_w[2])
                      << std::endl;
        }

        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

        // Load any pending geometry to the GPU.
        if (d_cam.IsShown()) {
            d_cam.Activate();

            if (cull_backfaces) {
                glEnable(GL_CULL_FACE);
                glCullFace(GL_BACK);
            }
            default_prog.Bind();
            default_prog.SetUniform("KT_cw",  s_cam.GetProjectionMatrix() *  s_cam.GetModelViewMatrix());
            pangolin::GlDraw( default_prog, geomToRender, nullptr);
            default_prog.Unbind();

            std::cout << "Transformation Matrix " << transformation_matrix_csv_path << ": " << transformation << std::endl;

            pangolin::OpenGlMatrix mv_mat = s_cam.GetModelViewMatrix();
            std::cout << "Original Point: " << mv_mat(0, 3) << ", " <<  mv_mat(1, 3) << ", " <<  mv_mat(2, 3) << std::endl;

            Eigen::Matrix4f mv_mat_eigen = openGlMatrixToEigen(mv_mat);
            Eigen::Vector4f pos_before_transform = Eigen::Vector4f(mv_mat_eigen(0, 3), mv_mat_eigen(1, 3), mv_mat_eigen(2, 3), 1.0);
            Eigen::Vector4f transformed_point = inverseTransformPoint(pos_before_transform, transformation);
            Eigen::Vector3f pos_after_transform = Eigen::Vector3f(pos_before_transform(0)- 0.6691778, pos_before_transform(1)+1.22925615, pos_before_transform[2]+2.24406284);
            Eigen::Matrix3f rot_mat;
            rot_mat << 0.97972727, -0.03784983, -0.19672792, -0.069904, -0.98485774, -0.15864633, -0.18774428,  0.1691822, -0.96753784;
            pos_after_transform = rot_mat.inverse() * pos_after_transform;
            pos_after_transform *= (1/6.2854950175989694);
            Eigen::Vector3f position(transformed_point(0), transformed_point(1), transformed_point(2));

            std::cout << "Transformed Point1: " << position(0) << ", " << position(1) << ", " << position(2) << std::endl;
            std::cout << "Transformed Point2: " << pos_after_transform << std::endl;

            // Extract rotation part of the transformed matrix
            Eigen::Matrix3f rotation_matrix = mv_mat_eigen.block<3, 3>(0, 0);

            // Convert the rotation matrix to Euler angles (yaw, pitch, roll)
            Eigen::Vector3f euler_angles = rotation_matrix.eulerAngles(2, 1, 0); // yaw, pitch, roll

            // Extract transformed yaw, pitch, and roll
            float yaw = euler_angles(0);
            float pitch = euler_angles(1);
            float roll = euler_angles(2);

            // Construct the original rotation matrix from the yaw, pitch, and roll angles
            Eigen::AngleAxisf yaw_rotation(yaw, Eigen::Vector3f::UnitZ());
            Eigen::AngleAxisf pitch_rotation(pitch, Eigen::Vector3f::UnitY());
            Eigen::AngleAxisf roll_rotation(roll, Eigen::Vector3f::UnitX());
            Eigen::Matrix3f original_rotation_matrix = (yaw_rotation * pitch_rotation * roll_rotation).toRotationMatrix();

            // Convert the original rotation matrix to Matrix4f for multiplication
            Eigen::Matrix4f original_rotation_matrix_4f = Eigen::Matrix4f::Identity();
            original_rotation_matrix_4f.block<3, 3>(0, 0) = original_rotation_matrix;

            // Apply the transformation matrix to obtain the resulting rotation matrix
            Eigen::Matrix4f transformed_rotation_matrix = transformation.inverse() * original_rotation_matrix_4f;

            // Extract the yaw, pitch, and roll angles from the resulting rotation matrix
            Eigen::Matrix3f transformed_rotation_matrix_3f = transformed_rotation_matrix.block<3, 3>(0, 0);
            Eigen::Vector3f transformed_euler_angles = transformed_rotation_matrix_3f.eulerAngles(2, 1, 0);
            float transformed_yaw = transformed_euler_angles(0);
            float transformed_pitch = transformed_euler_angles(1);
            float transformed_roll = transformed_euler_angles(2);

            // Run GetPointsFromPos
            std::string map_input_dir = data["mapInputDir"];
            const std::string cloud_points = map_input_dir + "cloud1.csv";
            std::vector<cv::Point3d> seen_points = Auxiliary::getPointsFromPos(cloud_points, cv::Point3d(position[0], position[1], position[2]), transformed_yaw, transformed_pitch, transformed_roll, Twc);
            std::vector<cv::Point3d> points_to_draw = convert_points(seen_points, transformation);

            s_cam.Apply();

            glDisable(GL_CULL_FACE);

            drawPoints(std::vector<cv::Point3d>(), points_to_draw);
        }

        pangolin::FinishFrame();
    }

    return 0;
}
