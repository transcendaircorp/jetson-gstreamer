#include <gst/gst.h>
#include <stdbool.h>

typedef struct _CameraData {
  GstElement *pipeline;
  GstElement *source;
  GstElement *filter;
  GstElement *pay;
  GstElement *identity;
  GstElement *udpsink;
} CameraData;

void init_camera_Data(CameraData *data) {
  data->pipeline = gst_pipeline_new("pipeline");
  if (!data->pipeline)
    g_error("Could not create 'pipeline'");
  data->source = gst_element_factory_make("v4l2src", "src");
  if (!data->source)
    g_error("Could not create 'v4l2src' element");
  data->filter = gst_element_factory_make("capsfilter", "filter");
  if (!data->filter)
    g_error("Could not create 'capsfilter' element");
  data->pay = gst_element_factory_make("rtpjpegpay", "pay");
  if (!data->pay)
    g_error("Could not create 'rtpjpegpay' element");
  data->identity = gst_element_factory_make("identity", "identity");
  if (!data->identity)
    g_error("Could not create 'identity' element");
  data->udpsink = gst_element_factory_make("multiudpsink", "udpsink");
  if (!data->udpsink)
    g_error("Could not create 'multiudpsink' element");

  /* Configure elements */
  g_object_set(G_OBJECT(data->source), "device", "/dev/video0", NULL);
  g_object_set(G_OBJECT(data->source), "io-mode", 2, NULL);
  GstCaps *filtercaps = gst_caps_new_simple("image/jpeg",                          //
                                            "width", G_TYPE_INT, 1920,             //
                                            "height", G_TYPE_INT, 1080,            //
                                            "framerate", GST_TYPE_FRACTION, 60, 1, //
                                            "format", G_TYPE_STRING,               //
                                            "MJPG", NULL);
  g_object_set(G_OBJECT(data->filter), "caps", filtercaps, NULL);
  gst_caps_unref(filtercaps);
  g_object_set(G_OBJECT(data->identity), "drop-allocation", 1, NULL);
  g_object_set(G_OBJECT(data->udpsink), "clients", "10.200.10.40:5000", NULL);
  g_object_set(G_OBJECT(data->udpsink), "sync", false, NULL);
  g_object_set(G_OBJECT(data->udpsink), "async", false, NULL);
  
  /* Build the pipeline */
  gst_bin_add_many(GST_BIN(data->pipeline), data->source, data->filter, data->pay, data->identity, data->udpsink, NULL);
  if (!gst_element_link_many(data->source, data->filter, data->pay, data->identity, data->udpsink, NULL))
    g_error("Failed to link elements");
}

void add_client(CameraData *data, gchar *client) {
  // get current clients
  gchar *clientstr;
  g_object_get(G_OBJECT(data->udpsink), "clients", &clientstr, NULL);
  gchar **clients = g_strsplit(clientstr, ",", 0);
  free(clientstr);

  // print current clients
  g_print("Current clients: ");
  for (int i = 0; clients[i] != NULL; i++)
    g_print("%s, ", clients[i]);
  g_print("\n");

  // add new client
  clients = g_realloc(clients, sizeof(gchar *) * (g_strv_length(clients) + 2));
  clients[g_strv_length(clients) - 1] = g_strdup(client);
  clients[g_strv_length(clients)] = NULL;

  // set new clients
  clientstr = g_strjoinv(",", clients);
  g_object_set(G_OBJECT(data->udpsink), "clients", clientstr, NULL);
  
  // free resources
  free(clientstr);
  g_strfreev(clients);
}

int main(int argc, char *argv[]) {
  CameraData data;
  GstBus *bus;
  GstMessage *msg;

  /* Initialize GStreamer */
  gst_init(&argc, &argv);

  /* Build the pipeline */
  init_camera_Data(&data);

  /* Start playing */
  gst_element_set_state(data.pipeline, GST_STATE_PLAYING);

  /* wait for a a line on stdin */
  g_print("Please enter a destiation ip:port\n");
  char ip[20];
  scanf("%20s", ip);
  add_client(&data, ip);

  /* Wait until error or EOS */
  bus = gst_element_get_bus(data.pipeline);
  msg = gst_bus_timed_pop_filtered(bus, GST_CLOCK_TIME_NONE, GST_MESSAGE_ERROR | GST_MESSAGE_EOS);

  /* See next tutorial for proper error message handling/parsing */
  if (GST_MESSAGE_TYPE(msg) == GST_MESSAGE_ERROR) {
    g_error("An error occurred! Re-run with the GST_DEBUG=*:WARN environment "
            "variable set for more details.");
  }

  /* Free resources */
  gst_message_unref(msg);
  gst_object_unref(bus);
  gst_element_set_state(data.pipeline, GST_STATE_NULL);
  gst_object_unref(data.pipeline);
  return 0;
}