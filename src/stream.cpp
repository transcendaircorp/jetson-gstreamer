#include <algorithm>
#include <arpa/inet.h>
#include <cxxopts.hpp>
#include <exception>
#include <fstream>
#include <gst/gst.h>
#include <iostream>
#include <netinet/in.h>
#include <regex>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

std::string string_join(const std::vector<std::string> &strings, const std::string &delimiter) {
  std::string result;
  for (const auto &string : strings)
    result += string + delimiter;
  return result.substr(0, result.length() - delimiter.length());
}

struct Client {
  sockaddr_in addr;
  Client(std::string ip, int port) {
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    inet_aton(ip.c_str(), &addr.sin_addr);
  }
  std::string toString() {
    char ip[20];
    inet_ntop(AF_INET, &addr.sin_addr, ip, 20);
    return std::string(ip) + ":" + std::to_string(ntohs(addr.sin_port));
  }
  bool operator==(const Client &other) const {
    return addr.sin_addr.s_addr == other.addr.sin_addr.s_addr && addr.sin_port == other.addr.sin_port;
  }
};

class CameraData {
public:
  // Static options
  std::string cameraPath;
  int framerate;
  int width;
  int height;

  // Parse video from webcam
  GstElement *pipeline = NULL;
  GstElement *source = NULL;
  GstElement *sourceFilter = NULL;
  GstElement *videoTee = NULL;
  // Send video to network
  GstElement *rtpQueue = NULL;
  GstElement *rtpPay = NULL;
  GstElement *identity = NULL;
  GstElement *udpsink = NULL;
  // Send video to file
  GstElement *recordQueue = NULL;
  // GstElement *jpegEnc = NULL;
  // GstElement *aviMux = NULL;
  GstElement *fileSink = NULL;
  // Clients
  std::vector<Client> clients;

  ~CameraData() {
    // release source
    if (pipeline)
      gst_object_unref(pipeline);
    if (source)
      gst_object_unref(source);
    if (sourceFilter)
      gst_object_unref(sourceFilter);
    if (videoTee)
      gst_object_unref(videoTee);

    // release rtp stream
    if (rtpQueue)
      gst_object_unref(rtpQueue);
    if (rtpPay)
      gst_object_unref(rtpPay);
    if (identity)
      gst_object_unref(identity);
    if (udpsink)
      gst_object_unref(udpsink);

    // release file sink

    if (recordQueue)
      gst_object_unref(recordQueue);
    // if (jpegEnc)
    //   gst_object_unref(jpegEnc);
    // if (aviMux)
    //   gst_object_unref(aviMux);
    if (fileSink)
      gst_object_unref(fileSink);
  }

  int init() {
    pipeline = gst_pipeline_new("pipeline");
    if (!pipeline)
      g_printerr("Could not create 'pipeline'");
    source = gst_element_factory_make("v4l2src", "src");
    if (!source)
      g_printerr("Could not create 'v4l2src' element");
    sourceFilter = gst_element_factory_make("capsfilter", "filter");
    if (!sourceFilter)
      g_printerr("Could not create 'capsfilter' element");
    videoTee = gst_element_factory_make("tee", "tee");
    if (!videoTee)
      g_printerr("Could not create 'tee' element");

    gst_bin_add_many(GST_BIN(pipeline), source, sourceFilter, videoTee, NULL);
    if (!gst_element_link_many(source, sourceFilter, videoTee, NULL)) {
      g_printerr("Failed to link source");
      return -1;
    }

    rtpQueue = gst_element_factory_make("queue", "rtpQueue");
    if (!rtpQueue)
      g_printerr("Could not create 'queue' element");
    rtpPay = gst_element_factory_make("rtpjpegpay", "rtpPay");
    if (!rtpPay)
      g_printerr("Could not create 'rtpjpegpay' element");
    identity = gst_element_factory_make("identity", "identity");
    if (!identity)
      g_printerr("Could not create 'identity' element");
    udpsink = gst_element_factory_make("multiudpsink", "udpSink");
    if (!udpsink)
      g_printerr("Could not create 'multiudpsink' element");
    gst_bin_add_many(GST_BIN(pipeline), rtpQueue, rtpPay, identity, udpsink, NULL);
    if (!gst_element_link_many(videoTee, rtpQueue, rtpPay, identity, udpsink, NULL)) {
      g_error("Failed to link network");
      return -1;
    }

    recordQueue = gst_element_factory_make("queue", "recordQueue");
    if (!recordQueue)
      g_printerr("Could not create 'queue' element");
    // jpegEnc = gst_element_factory_make("jpegenc", "jpegEnc");
    // if (!jpegEnc)
    //   g_printerr("Could not create 'jpegenc' element");
    // aviMux = gst_element_factory_make("avimux", "aviMux");
    // if (!aviMux)
    //   g_printerr("Could not create 'avimux' element");
    fileSink = gst_element_factory_make("filesink", "fileSink");
    if (!fileSink)
      g_printerr("Could not create 'filesink' element");
    gst_bin_add_many(GST_BIN(pipeline), recordQueue, fileSink, NULL);
    if (!gst_element_link_many(recordQueue, fileSink, NULL)) {
      g_error("Failed to link file");
      return -1;
    }

    if (!pipeline || !source || !sourceFilter || !videoTee || !rtpQueue || !rtpPay || !identity || !udpsink ||
        !recordQueue || /*!jpegEnc || !aviMux ||*/ !fileSink) {
      g_error("Not all elements could be created");
      return -1;
    }

    g_object_set(G_OBJECT(source), "device", cameraPath.c_str(), NULL);
    g_object_set(G_OBJECT(source), "io-mode", 2, NULL);
    GstCaps *filtercaps = gst_caps_new_simple("image/jpeg",                                 //
                                              "width", G_TYPE_INT, width,                   //
                                              "height", G_TYPE_INT, height,                 //
                                              "framerate", GST_TYPE_FRACTION, framerate, 1, //
                                              "format", G_TYPE_STRING, "MJPG",              //
                                              NULL);
    g_object_set(G_OBJECT(sourceFilter), "caps", filtercaps, NULL);
    gst_caps_unref(filtercaps);

    g_object_set(G_OBJECT(fileSink), "location", "/dev/null", NULL);

    g_object_set(G_OBJECT(identity), "drop-allocation", 1, NULL);
    g_object_set(G_OBJECT(udpsink), "auto-multicast", true, NULL);
    g_object_set(G_OBJECT(udpsink), "sync", false, NULL);
    g_object_set(G_OBJECT(udpsink), "async", false, NULL);
    updateClients();
    return 0;
  }

  // manage pipeline
  GstStateChangeReturn play() { return gst_element_set_state(pipeline, GST_STATE_PLAYING); }
  void pause() { gst_element_set_state(pipeline, GST_STATE_PAUSED); }
  void stop() { gst_element_set_state(pipeline, GST_STATE_NULL); }
  int startRecord(std::string filename) {
    gst_element_set_state(fileSink, GST_STATE_NULL);
    g_object_set(G_OBJECT(fileSink), "location", filename.append(".mjpg").c_str(), NULL);
    gst_element_set_state(fileSink, GST_STATE_PLAYING);
    gst_element_link_many(videoTee, recordQueue, NULL);
    return 0;
  }
  bool stopRecord() {
    // send eos to avimux
    gst_element_unlink_many(videoTee, recordQueue, NULL);
    return true;
  }

  // pipline utils
  GstBus *getBus() { return gst_element_get_bus(pipeline); }

  // client utils
  bool addClient(Client client) {
    // check if client already exists
    for (auto c : clients)
      if (c == client)
        return false;
    clients.push_back(client);
    updateClients();
    return true;
  }
  bool removeClient(Client client) {
    auto old_size = clients.size();
    clients.erase(std::remove(clients.begin(), clients.end(), client), clients.end());
    auto changed = clients.size() != old_size;
    if (changed)
      updateClients();
    return changed;
  }

private:
  void updateClients() {
    std::string result;
    for (auto c : clients)
      result += c.toString() + ",";
    result.pop_back();
    if (udpsink)
      g_object_set(G_OBJECT(udpsink), "clients", result.c_str(), NULL);
  }
};

void inputLoop(CameraData *camera) {
  while (true) {
    std::string input;
    std::getline(std::cin, input);
    std::vector<std::string> args;
    std::istringstream iss(input);
    for (std::string s; iss >> s;)
      args.push_back(s);
    if (args.size() == 0)
      continue;
    std::transform(args[0].begin(), args[0].end(), args[0].begin(), ::tolower);
    if (args[0] == "help") {
      std::cout << "Available commands:" << std::endl;
    } else if (args[0] == "play") {
      camera->play();
    } else if (args[0] == "pause") {
      camera->pause();
    } else if (args[0] == "stop") {
      camera->stop();
    } else if (args[0] == "addclient") {
      if (args.size() != 3) {
        std::cout << "Usage: addclient <ip> <port>" << std::endl;
        continue;
      }
      camera->addClient(Client(args[1], std::stoi(args[2])));
    } else if (args[0] == "removeclient") {
      if (args.size() != 3) {
        std::cout << "Usage: removeclient <ip> <port>" << std::endl;
        continue;
      }
      camera->removeClient(Client(args[1], std::stoi(args[2])));
    } else if (args[0] == "record") {
      if (args.size() != 2) {
        std::cout << "Usage: record <filename>" << std::endl;
        continue;
      }
      // check to make sure filepath is valid
      std::ofstream file(args[1] + ".mjpg");
      if (!file.good()) {
        std::cout << "Invalid filepath" << std::endl;
        file.close();
        continue;
      }
      file.close();
      camera->startRecord(args[1]);
    } else if (args[0] == "stoprecord") {
      camera->stopRecord();
    } else if (args[0] == "exit") {
      break;
    } else {
      std::cout << "Unknown command" << std::endl;
    }
  }
}

int parseArgs(cxxopts::ParseResult result, CameraData &camera) {
  if (result.count("help")) {
    return 1;
  }
  std::string cameraPath;
  try {
    cameraPath = result["camera"].as<std::string>();
  } catch (cxxopts::exceptions::option_has_no_value e) {
    std::cout << "Camera path required" << std::endl;
    return 1;
  } catch (cxxopts::exceptions::exception e) {
    std::cout << e.what() << std::endl;
    return 1;
  }
  std::ifstream file(cameraPath);
  if (!file.good()) {
    std::cout << "Invalid camera path" << std::endl;
    return 1;
  }
  camera.cameraPath = cameraPath;

  std::string resolution;
  try {
    resolution = result["resolution"].as<std::string>();
  } catch (cxxopts::exceptions::option_has_no_value e) {
    std::cout << "Resolution required" << std::endl;
    return 1;
  } catch (cxxopts::exceptions::exception e) {
    std::cout << e.what() << std::endl;
    return 1;
  }
  std::regex resRegex("([0-9]+)x([0-9]+)");
  std::smatch resMatch;
  if (!std::regex_match(resolution, resMatch, resRegex)) {
    std::cout << "Invalid resolution format" << std::endl;
    return 1;
  }
  camera.width = std::stoi(resMatch[1]);
  camera.height = std::stoi(resMatch[2]);

  int framerate;
  try {
    framerate = result["framerate"].as<int>();
  } catch (cxxopts::exceptions::option_has_no_value e) {
    std::cout << "Framerate required" << std::endl;
    return 1;
  } catch (cxxopts::exceptions::exception e) {
    std::cout << e.what() << std::endl;
    return 1;
  }
  camera.framerate = framerate;

  std::vector<std::string> clients;
  try {
    clients = result["address"].as<std::vector<std::string>>();
  } catch (cxxopts::exceptions::option_has_no_value e) {
  } catch (cxxopts::exceptions::exception e) {
    std::cout << e.what() << std::endl;
    return 1;
  }
  for (auto const client : clients) {
    std::regex clientRegex("([0-9]{1,3}(?:\\.[0-9]{1,3}){3}):([0-9]+)");
    std::smatch clientMatch;
    if (!std::regex_match(client, clientMatch, clientRegex)) {
      std::cout << "Invalid client: " << client << std::endl;
      return 1;
    }
    camera.addClient(Client(clientMatch[1], std::stoi(clientMatch[2])));
  }
  return 0;
}

GMainLoop *loop;
CameraData camera;
static gboolean message_cb(GstBus *bus, GstMessage *message, gpointer user_data) {
  switch (GST_MESSAGE_TYPE(message)) {
  case GST_MESSAGE_ERROR: {
    GError *err = NULL;
    gchar *name, *debug = NULL;

    name = gst_object_get_path_string(message->src);
    gst_message_parse_error(message, &err, &debug);

    g_printerr("ERROR: from element %s: %s\n", name, err->message);
    if (debug != NULL)
      g_printerr("Additional debug info:\n%s\n", debug);

    g_error_free(err);
    g_free(debug);
    g_free(name);

    g_main_loop_quit(loop);
    break;
  }
  case GST_MESSAGE_WARNING: {
    GError *err = NULL;
    gchar *name, *debug = NULL;

    name = gst_object_get_path_string(message->src);
    gst_message_parse_warning(message, &err, &debug);

    g_printerr("ERROR: from element %s: %s\n", name, err->message);
    if (debug != NULL)
      g_printerr("Additional debug info:\n%s\n", debug);

    g_error_free(err);
    g_free(debug);
    g_free(name);
    break;
  }
  case GST_MESSAGE_STATE_CHANGED: {
    /* We are only interested in state-changed messages from the pipeline */
    if (GST_MESSAGE_SRC(message) == GST_OBJECT(camera.pipeline)) {
      GstState old_state, new_state, pending_state;
      gst_message_parse_state_changed(message, &old_state, &new_state, &pending_state);
      g_print("Pipeline state changed from %s to %s:\n", gst_element_state_get_name(old_state),
              gst_element_state_get_name(new_state));
    }
    break;
  }
  case GST_MESSAGE_EOS: {
    g_print("Got EOS\n");
    g_main_loop_quit(loop);
    break;
  }
  default:
    break;
  }
  return TRUE;
}

int main(int argc, char *argv[]) {
  cxxopts::Options options("cam2rtpfile",
                           "Takes a camera input and streams it over udp with rtp, and optionally records to a file");
  options.add_options()                                                                                  //
      ("c,camera", "Path to camera device, i.e. /dev/video0", cxxopts::value<std::string>())             //
      ("f,framerate", "Framerate for the video source", cxxopts::value<int>())                           //
      ("r,resolution", "Resolution for the video source, i.e. 1920x1080", cxxopts::value<std::string>()) //
      ("a,address",
       "List of udp addresses for stream, i.e. 10.0.0.1:1924,10.0.0.2:1925. Can be added and removed later.",
       cxxopts::value<std::vector<std::string>>()) //
      ("h,help", "Print this help message");
  try {
    auto result = options.parse(argc, argv);
    if (parseArgs(result, camera)) {
      std::cout << std::endl << options.help() << std::endl;
      return 1;
    }
  } catch (cxxopts::exceptions::parsing e) {
    std::cout << e.what() << std::endl << std::endl << options.help() << std::endl;
    return 1;
  }

  /* Initialize GStreamer */
  gst_init(&argc, &argv);

  /* Create the empty pipeline */
  loop = g_main_loop_new(NULL, FALSE);

  // start input thread
  std::thread inputThread(inputLoop, &camera);

  camera.init();

  /* Add a bus watch, so we get notified when a message arrives */
  GstBus *bus = camera.getBus();
  gst_bus_add_watch(bus, message_cb, NULL);
  gst_object_unref(bus);

  /* Start playing */
  camera.play();

  /* Run event loop listening for bus messages until EOS or ERROR */
  g_print("Starting loop\n");
  g_main_loop_run(loop);

  /* Free resources */
  gst_object_unref(bus);
  camera.stop();
  delete &camera;
  inputThread.join();
  return 0;
}