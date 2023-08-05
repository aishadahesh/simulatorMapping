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

#include <pangolin/geometry/geometry_ply.h>
#include <pangolin/geometry/glgeometry.h>

#include "include/run_model/TextureShader.h"
#include "include/Auxiliary.h"

#include <Eigen/SVD>
#include <Eigen/Geometry>

#include <opencv2/core.hpp>
#include <opencv2/opencv.hpp>
#include <opencv2/calib3d.hpp>

Eigen::Matrix4d read_matrix_4d_from_csv(std::string filename)
{
    Eigen::Matrix4d matrix;

    std::ifstream csv_file(filename);

    if (!csv_file.is_open()) {
        std::cerr << "Error: could not open file '" << filename << "' for reading." << std::endl;
        return matrix;
    }

    // Read matrix elements from file
    std::string line;
    int row = 0;
    while (std::getline(csv_file, line) && row < 4) {
        std::stringstream line_stream(line);
        std::string cell;
        int col = 0;
        while (std::getline(line_stream, cell, ',') && col < 4) {
            matrix(row, col) = std::stod(cell);
            col++;
        }
        row++;
    }

    csv_file.close();

    return matrix;
}


int main(int argc, char **argv) {

    using namespace pangolin;

    std::string settingPath = Auxiliary::GetGeneralSettingsPath();
    std::ifstream programData(settingPath);
    nlohmann::json data;
    programData >> data;
    programData.close();

    std::string configPath = data["DroneYamlPathSlam"];
    cv::FileStorage fSettings(configPath, cv::FileStorage::READ);

    // Read matrices
    int frame_to_check = data["frameNumber"];
    std::string mv_filename = std::string(data["framesOutput"]) + "frame_" + std::to_string(frame_to_check) + "_mv.csv";
    Eigen::Matrix4d mv_mat = read_matrix_4d_from_csv(mv_filename);
    std::string proj_filename = std::string(data["framesOutput"]) + "frame_" + std::to_string(frame_to_check) + "_proj.csv";
    Eigen::Matrix4d proj_mat = read_matrix_4d_from_csv(proj_filename);
    
    float fx = fSettings["Camera.fx"];
    float fy = fSettings["Camera.fy"];
    float cx = fSettings["Camera.cx"];
    float cy = fSettings["Camera.cy"];
    float viewpointX = fSettings["RunModel.ViewpointX"];
    float viewpointY = fSettings["RunModel.ViewpointY"];
    float viewpointZ = fSettings["RunModel.ViewpointZ"];

    Eigen::Matrix3d K;
    K << fx, 0.0, cx, 0.0, fy, cy, 0.0, 0.0, 1.0;
    Eigen::Vector2f viewport_size(fSettings["Camera.width"], fSettings["Camera.height"]);


    // Options
    bool show_bounds = false;
    bool show_axis = false;
    bool show_x0 = false;
    bool show_y0 = false;
    bool show_z0 = false;
    bool cull_backfaces = false;

    // Create Window for rendering
    pangolin::CreateWindowAndBind("Main", viewport_size[0], viewport_size[1]);
    glEnable(GL_DEPTH_TEST);

    // Define Projection and initial ModelView matrix
    pangolin::OpenGlRenderState s_cam(
            pangolin::ProjectionMatrix(viewport_size(0), viewport_size(1), K(0, 0), K(1, 1), K(0, 2), K(1, 2), 0.1, 10000),
            pangolin::ModelViewLookAt(viewpointX, viewpointY, viewpointZ, 0, 0, 0, 0.0, -1.0, pangolin::AxisY)
    );

    // Create Interactive View in window
    pangolin::Handler3D handler(s_cam);
    pangolin::View &d_cam = pangolin::CreateDisplay()
            .SetBounds(0.0, 1.0, 0.0, 1.0, -viewport_size[0] / viewport_size[1])
            .SetHandler(&handler);

    // Load Geometry asynchronously
    std::string model_path = data["modelPath"];
    const pangolin::Geometry geom_to_load = pangolin::LoadGeometry(model_path);
    auto aabb = pangolin::GetAxisAlignedBox(geom_to_load);
    Eigen::AlignedBox3f total_aabb;
    total_aabb.extend(aabb);
    s_cam.SetModelViewMatrix(mv_mat);
    s_cam.SetProjectionMatrix(proj_mat);
    const pangolin::GlGeometry geomToRender = pangolin::ToGlGeometry(geom_to_load);
    // Render tree for holding object position
    pangolin::GlSlProgram default_prog;
    auto LoadProgram = [&]() {
        default_prog.ClearShaders();
        default_prog.AddShader(pangolin::GlSlAnnotatedShader, shader);
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

    Eigen::Vector3d Pick_w = handler.Selected_P_w();
    std::vector<Eigen::Vector3d> Picks_w;
    cv::Mat img;

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

            s_cam.Apply();

            glDisable(GL_CULL_FACE);

            pangolin::Image<unsigned char> buffer;
            pangolin::VideoPixelFormat fmt = pangolin::VideoFormatFromString("RGBA32");
            buffer.Alloc(viewport_size[0], viewport_size[1], viewport_size[0] * fmt.bpp/8 );
            glReadBuffer(GL_BACK);
            glPixelStorei(GL_PACK_ALIGNMENT, 1);
            glReadPixels(0, 0, viewport_size[0], viewport_size[1], GL_RGBA, GL_UNSIGNED_BYTE, buffer.ptr );
 
            cv::Mat  imgBuffer = cv::Mat(viewport_size[1], viewport_size[0], CV_8UC4, buffer.ptr);
            cv::cvtColor(imgBuffer, img,  cv::COLOR_RGBA2BGR);
            cv::flip(img, img, 0);

            pangolin::FinishFrame();
            break;
        }
    }

    std::string frame_location = std::string(data["framesOutput"]) + "frame_" + std::to_string(frame_to_check) + ".png";
    cv::imwrite(frame_location, img);

    return 0;
}
