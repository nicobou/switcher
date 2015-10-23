#include "./posture_meshgpucreator.hpp"
#include "switcher/std2.hpp"

#include <functional>
#include <iostream>

//#include <boost/make_shared.hpp>
#include <pcl/io/obj_io.h>

using namespace std;
using namespace posture;

namespace switcher {
  SWITCHER_MAKE_QUIDDITY_DOCUMENTATION (
					PostureMeshGPUCreator,
                    "posturemeshgpucreator",
					"Scan 3D GPU",
					"video",
                    "writer/device",
					"Grab mesh using zcameras on GPU",
					"LGPL",
					"Ludovic Schreiber");

  PostureMeshGPUCreator::PostureMeshGPUCreator(const std::string &) {
    init_startable (this);
    mesh_creator_ = std::unique_ptr<DepthMapToMesh> (new DepthMapToMesh ());

    pmanage<MPtr(&PContainer::make_int)>
            ("camera_number",
             [this](const int &val) {
                nbr_cam_ = val;
                return true;
             },
             [this]() {return nbr_cam_;},
             "Camera number",
             "Number of used cameras",
             nbr_cam_,
             1,
             9);
    
    pmanage<MPtr(&PContainer::make_string)>
            ("calibration_path",
             [this](const std::string &val) {
                calibration_path_ = val;
                return true;
             },
             [this]() {return calibration_path_;},
             "calibration_path",
             "Path to the calibration file",
             calibration_path_);

//    pmanage<MPtr(&PContainer::make_string)>
//            ("devices_path",
//             [this](const std::string &val)
//             {
//                devices_path_ = val;
//                return true;
//             },
//             [this](){return devices_path_;},
//             "devices_path",
//             "Path to the devices description file",
//             devices_path_);

    pmanage<MPtr(&PContainer::make_bool)>
            ("save_mesh",
             [this](bool val) {
                save_mesh_ = val;
                return true;
             },
             [this]() {return save_mesh_;},
             "save_mesh",
             "Save the current mesh if true",
             save_mesh_);

    pmanage<MPtr(&PContainer::make_double)>
            ("grid resolution",
             [this](double val) {
                resolution_ = val;
                mesh_creator_->setGridResolution(resolution_);
                return true;
             },
             [this]() {return resolution_;},
             "grid resolution",
             "resolution in mm for the mesh reconstruction",
             resolution_,
             10.0f,
             100.0f);

    pmanage<MPtr(&PContainer::make_double)>
            ("grid size",
             [this](double val) {
                size_ = val;
                sizeX_ = val;
                sizeY_ = val;
                sizeZ_ = val;
//                mesh_creator_->setGridSize(size_);
                mesh_creator_->setGridSizeX(sizeX_);
                mesh_creator_->setGridSizeY(sizeY_);
                mesh_creator_->setGridSizeZ(sizeZ_);
                pmanage<MPtr(&PContainer::notify)>(sizeX_id_);
                pmanage<MPtr(&PContainer::notify)>(sizeY_id_);
                pmanage<MPtr(&PContainer::notify)>(sizeZ_id_);
                return true;
             },
             [this]() {return size_;},
             "grid size",
             "size of the cube for the mesh reconstruction",
             size_,
             0.0f,
             10.0f);

    sizeX_id_ = pmanage<MPtr(&PContainer::make_double)>
            ("grid size X",
             [this](double val) {
                sizeX_ = val;
                mesh_creator_->setGridSizeX(sizeX_);
                return true;
             },
             [this]() {return sizeX_;},
             "grid size X",
             "size of the cube for the mesh reconstruction",
             sizeX_,
             0.0f,
             10.0f);

    sizeY_id_ = pmanage<MPtr(&PContainer::make_double)>
            ("grid size Y",
             [this](double val) {
                sizeY_ = val;
                mesh_creator_->setGridSizeY(sizeY_);
                return true;
             },
             [this]() {return sizeY_;},
             "grid size Y",
             "size of the cube for the mesh reconstruction",
             sizeY_,
             0.0f,
             10.0f);

    sizeZ_id_ = pmanage<MPtr(&PContainer::make_double)>
            ("grid size Z",
             [this](double val) {
                sizeZ_ = val;
                mesh_creator_->setGridSizeZ(sizeZ_);
                return true;
             },
             [this]() {return sizeZ_;},
             "grid size Z",
             "size of the cube for the mesh reconstruction",
             sizeZ_,
             0.0f,
             10.0f);
  }

  PostureMeshGPUCreator::~PostureMeshGPUCreator() {
    stop();
  }

  bool
  PostureMeshGPUCreator::start() {
    std::lock_guard<std::mutex> lock (mutex_);
    cameras_.resize(nbr_cam_);
    mesh_creator_->setDepthMapNbr(nbr_cam_);
    mesh_creator_->setSaveMesh(save_mesh_);
    mesh_creator_->setCalibrationPath(calibration_path_);
    auto index = 0;
    for (; index < nbr_cam_; index++)
    {
      cameras_[index] = std2::make_unique<ZCamera>();
      cameras_[index]->setDeviceIndex(index);
      cameras_[index]->setCaptureMode(ZCamera::CaptureMode_QQVGA_30Hz);
      cameras_[index]->
      setCallbackDepth([=] (void*,
                            std::vector<unsigned char>& depth,
                            int width,
                            int height) -> void
      {
        cb_frame_depth(index, depth, width, height);
      }, nullptr);
      cameras_[index] -> start();
    }

    is_started_ = true;
    return true;
  }

  bool
  PostureMeshGPUCreator::stop() {
    std::lock_guard<std::mutex> lock (mutex_);
    auto index = 0;
    for (; index < nbr_cam_; index++)
    {
      cameras_ [index] -> stop ();
    }

//    mesh_writer_.reset();
    is_started_ = false;
    return true;
  }

  bool PostureMeshGPUCreator::init()
  {
    return true;
  }

  void
  PostureMeshGPUCreator::cb_frame_depth(int index, std::vector<unsigned char>& depth, int width, int height) {
//    std::lock_guard<std::mutex> lock (cb_depth_mutex_);

    if (!mutex_.try_lock())
    {
        std::lock_guard<std::mutex> lock (stock_mutex_);
        stock_[index]=depth;
        return;
    }

    if (stock_.size() > 0)
    {
      unique_lock<mutex> lock(stock_mutex_);
      for (auto it = stock_.begin(); it != stock_.end(); ++it)
      {
        mesh_creator_->setInputDepthMap(it->first, it->second, width, height);
      }
      stock_.clear();
    }
    mesh_creator_->setInputDepthMap(index, depth, width, height);
//    mesh_creator_->getMesh(output_);

    pcl::PolygonMesh::Ptr Mesh;
    Mesh = boost::make_shared<pcl::PolygonMesh>();
    mesh_creator_->getMesh(Mesh);

//    if (!mesh_writer_ || output_.size() > mesh_writer_->writer<MPtr(&shmdata::Writer::alloc_size)>())
//    {
//      mesh_writer_.reset();
//      mesh_writer_ = std2::make_unique<ShmdataWriter>(this,
//						      make_file_name("mesh"),
//						      output_.size() * 2,
//						      string(POLYGONMESH_TYPE_BASE));
//      if (!mesh_writer_)
//      {
//        g_warning("Unable to create mesh callback");
//        return;
//      }
//    }
//    mesh_writer_->writer<MPtr(&shmdata::Writer::copy_to_shm)>(const_cast<unsigned char*>(output_.data()),
//							      output_.size());
//    mesh_writer_->bytes_written(output_.size());

//    _disp.setPolygonMesh(output_);
    _disp.setPolygonMesh(Mesh);
    mutex_.unlock();
  }

  void
  PostureMeshGPUCreator::cb_frame_cloud(int index, std::vector<char>& cloud)  {}

  void
  PostureMeshGPUCreator::cb_frame_RGB(int index, std::vector<unsigned char>& rgb, int width, int height) {}

}  // namespace switcher
