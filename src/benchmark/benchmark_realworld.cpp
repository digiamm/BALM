#include "tools.hpp"
#include <ros/ros.h>
#include <Eigen/Eigenvalues>
#include <Eigen/Core>
#include <sensor_msgs/PointCloud2.h>
#include <pcl_conversions/pcl_conversions.h>
#include <geometry_msgs/PoseArray.h>
#include <random>
#include <ctime>
#include <tf/transform_broadcaster.h>
#include "bavoxel.hpp"

#include <pcl/filters/voxel_grid.h>
#include <pcl/io/pcd_io.h>
#include <malloc.h>

#include <fstream>
#include <iomanip>

using namespace std;

template <typename T>
void pub_pl_func(T &pl, ros::Publisher &pub)
{
  pl.height = 1;
  pl.width = pl.size();
  sensor_msgs::PointCloud2 output;
  pcl::toROSMsg(pl, output);
  output.header.frame_id = "camera_init";
  output.header.stamp = ros::Time::now();
  pub.publish(output);
}

ros::Publisher pub_path, pub_test, pub_show;

int read_pose(vector<double> &tims, PLM(3) & rots, PLV(3) & poss, string readname)
{
  // string readname = prename + "alidarPose.csv";

  cout << readname << endl;
  ifstream inFile(readname);

  if (!inFile.is_open())
  {
    printf("open fail\n");
    return 0;
  }

  int pose_size = 0;
  string lineStr, str;
  Eigen::Matrix4d aff;
  vector<double> nums;

  int ord = 0;
  while (getline(inFile, lineStr))
  {
    ord++;
    stringstream ss(lineStr);
    while (getline(ss, str, ','))
      nums.push_back(stod(str));

    if (ord == 4)
    {
      for (int j = 0; j < 16; j++)
        aff(j) = nums[j];

      Eigen::Matrix4d affT = aff.transpose();

      rots.push_back(affT.block<3, 3>(0, 0));
      poss.push_back(affT.block<3, 1>(0, 3));
      tims.push_back(affT(3, 3));
      nums.clear();
      ord = 0;
      pose_size++;
    }
  }

  return pose_size;
}

void read_file(vector<IMUST> &x_buf, vector<pcl::PointCloud<PointType>::Ptr> &pl_fulls, string &traj_name)
{
  // prename = prename + "/datas/benchmark_realworld/";

  PLV(3)
  poss;
  PLM(3)
  rots;
  vector<double> tims;

  size_t lastindex = traj_name.find_last_of("_");
  const string base_name = traj_name.substr(0, lastindex);

  cout << "trajectory path: "  << traj_name << endl;
  int pose_size = read_pose(tims, rots, poss, traj_name);

  for (int m = 0; m < pose_size; m++)
  {
    string filename = base_name + "_" + to_string(m) + ".pcd";
    cout << filename << endl;

    pcl::PointCloud<PointType>::Ptr pl_ptr(new pcl::PointCloud<PointType>());
    pcl::PointCloud<pcl::PointXYZI> pl_tem;
    pcl::io::loadPCDFile(filename, pl_tem);
    for (pcl::PointXYZI &pp : pl_tem.points)
    {
      PointType ap;
      ap.x = pp.x;
      ap.y = pp.y;
      ap.z = pp.z;
      ap.intensity = pp.intensity;
      pl_ptr->push_back(ap);
    }

    pl_fulls.push_back(pl_ptr);

    IMUST curr;
    curr.R = rots[m];
    curr.p = poss[m];
    curr.t = tims[m];
    x_buf.push_back(curr);
  }
}

void data_show(vector<IMUST> x_buf, vector<pcl::PointCloud<PointType>::Ptr> &pl_fulls)
{
  IMUST es0 = x_buf[0];
  for (uint i = 0; i < x_buf.size(); i++)
  {
    x_buf[i].p = es0.R.transpose() * (x_buf[i].p - es0.p);
    x_buf[i].R = es0.R.transpose() * x_buf[i].R;
  }

  pcl::PointCloud<PointType> pl_send, pl_path;
  int winsize = x_buf.size();
  for (int i = 0; i < winsize; i++)
  {
    pcl::PointCloud<PointType> pl_tem = *pl_fulls[i];
    down_sampling_voxel(pl_tem, 0.05);
    pl_transform(pl_tem, x_buf[i]);
    pl_send += pl_tem;

    if ((i % 200 == 0 && i != 0) || i == winsize - 1)
    {
      pub_pl_func(pl_send, pub_show);
      pl_send.clear();
      sleep(0.5);
    }

    PointType ap;
    ap.x = x_buf[i].p.x();
    ap.y = x_buf[i].p.y();
    ap.z = x_buf[i].p.z();
    ap.curvature = i;
    pl_path.push_back(ap);
  }

  pub_pl_func(pl_path, pub_path);
}

#include <chrono>


int main(int argc, char **argv)
{
  ros::init(argc, argv, "benchmark2");
  ros::NodeHandle n;
  pub_test = n.advertise<sensor_msgs::PointCloud2>("/map_test", 100);
  pub_path = n.advertise<sensor_msgs::PointCloud2>("/map_path", 100);
  pub_show = n.advertise<sensor_msgs::PointCloud2>("/map_show", 100);

  string prename, ofname;
  vector<IMUST> x_buf;
  vector<pcl::PointCloud<PointType>::Ptr> pl_fulls;

  n.param<double>("voxel_size", voxel_size, 1);
  string file_path, balm_trajectory;
  n.param<string>("file_path", file_path, "");
  n.param<string>("trajectory_output_path", balm_trajectory, "");
  
  using namespace std::chrono;
  auto start_load = high_resolution_clock::now();
  read_file(x_buf, pl_fulls, file_path);

  IMUST es0 = x_buf[0];
  for (uint i = 0; i < x_buf.size(); i++)
  {
    x_buf[i].p = es0.R.transpose() * (x_buf[i].p - es0.p);
    x_buf[i].R = es0.R.transpose() * x_buf[i].R;
  }

  win_size = x_buf.size();
  printf("The size of poses: %d\n", win_size);

  auto end_load = high_resolution_clock::now();
  auto duration_load = duration_cast<milliseconds>(end_load - start_load);
  std::cout << "load duration: " << duration_load.count() << " [ ms ] " << std::endl;
  
  auto start_opt = high_resolution_clock::now();
  pcl::PointCloud<PointType> pl_full, pl_surf, pl_path, pl_send;
  for (int iterCount = 0; iterCount < 1; iterCount++)
  {
    unordered_map<VOXEL_LOC, OCTO_TREE_ROOT *> surf_map;

    for (int i = 0; i < win_size; i++)
      cut_voxel(surf_map, *pl_fulls[i], x_buf[i], i);

    pcl::PointCloud<PointType> pl_cent;
    pl_send.clear();
    VOX_HESS voxhess;
    for (auto iter = surf_map.begin(); iter != surf_map.end() && n.ok(); iter++)
    {
      iter->second->recut(win_size);
      iter->second->tras_opt(voxhess, win_size);
    }

    data_show(x_buf, pl_fulls);
    printf("Initial point cloud is published.\n");
    // printf("Input '1' to start optimization...\n");
    // int a;
    // cin >> a;
    // if (a == 0)
    //   exit(0);

    BALM2 opt_lsv;
    opt_lsv.damping_iter(x_buf, voxhess);

    for (auto iter = surf_map.begin(); iter != surf_map.end();)
    {
      delete iter->second;
      surf_map.erase(iter++);
    }
    surf_map.clear();

    malloc_trim(0);
  }

  malloc_trim(0);
  data_show(x_buf, pl_fulls);
  printf("Refined point cloud is published.\n");

  auto end_opt = high_resolution_clock::now();
  auto duration_opt = duration_cast<milliseconds>(end_opt - start_opt);
  std::cout << "total duration: " << duration_opt.count() << " [ ms ] " << std::endl;

  // dump output to txt file
  ofstream os(balm_trajectory);
  os << fixed << setprecision(12);
  os << "# duration [ms] | load: " << duration_load.count() << " | opt: " << duration_opt.count() << std::endl; 
  os << fixed << setprecision(6);
  for (uint i = 0; i < x_buf.size(); i++)
  {
    Eigen::Quaterniond quat(x_buf[i].R);
    os << x_buf[i].t << " "
       << x_buf[i].p.transpose() << " "
       << quat.x() << " "
       << quat.y() << " "
       << quat.z() << " "
       << quat.w() << "\n";
  }
  cout << "trajectory output written in " << balm_trajectory << endl;
  os.close();

  return 0;
}
