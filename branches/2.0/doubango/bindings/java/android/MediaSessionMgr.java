/* ----------------------------------------------------------------------------
 * This file was automatically generated by SWIG (http://www.swig.org).
 * Version 1.3.39
 *
 * Do not make changes to this file unless you know what you are doing--modify
 * the SWIG interface file instead.
 * ----------------------------------------------------------------------------- */

package org.doubango.tinyWRAP;

public class MediaSessionMgr {
  private long swigCPtr;
  protected boolean swigCMemOwn;

  protected MediaSessionMgr(long cPtr, boolean cMemoryOwn) {
    swigCMemOwn = cMemoryOwn;
    swigCPtr = cPtr;
  }

  protected static long getCPtr(MediaSessionMgr obj) {
    return (obj == null) ? 0 : obj.swigCPtr;
  }

  protected void finalize() {
    delete();
  }

  public synchronized void delete() {
    if(swigCPtr != 0 && swigCMemOwn) {
      swigCMemOwn = false;
      tinyWRAPJNI.delete_MediaSessionMgr(swigCPtr);
    }
    swigCPtr = 0;
  }

  public boolean sessionSetInt32(twrap_media_type_t media, String key, int value) {
    return tinyWRAPJNI.MediaSessionMgr_sessionSetInt32(swigCPtr, this, media.swigValue(), key, value);
  }

  public boolean consumerSetInt32(twrap_media_type_t media, String key, int value) {
    return tinyWRAPJNI.MediaSessionMgr_consumerSetInt32(swigCPtr, this, media.swigValue(), key, value);
  }

  public boolean consumerSetInt64(twrap_media_type_t media, String key, long value) {
    return tinyWRAPJNI.MediaSessionMgr_consumerSetInt64(swigCPtr, this, media.swigValue(), key, value);
  }

  public boolean producerSetInt32(twrap_media_type_t media, String key, int value) {
    return tinyWRAPJNI.MediaSessionMgr_producerSetInt32(swigCPtr, this, media.swigValue(), key, value);
  }

  public boolean producerSetInt64(twrap_media_type_t media, String key, long value) {
    return tinyWRAPJNI.MediaSessionMgr_producerSetInt64(swigCPtr, this, media.swigValue(), key, value);
  }

  public ProxyPlugin findProxyPluginConsumer(twrap_media_type_t media) {
    long cPtr = tinyWRAPJNI.MediaSessionMgr_findProxyPluginConsumer(swigCPtr, this, media.swigValue());
    return (cPtr == 0) ? null : new ProxyPlugin(cPtr, false);
  }

  public ProxyPlugin findProxyPluginProducer(twrap_media_type_t media) {
    long cPtr = tinyWRAPJNI.MediaSessionMgr_findProxyPluginProducer(swigCPtr, this, media.swigValue());
    return (cPtr == 0) ? null : new ProxyPlugin(cPtr, false);
  }

  public java.math.BigInteger getSessionId(twrap_media_type_t media) {
    return tinyWRAPJNI.MediaSessionMgr_getSessionId(swigCPtr, this, media.swigValue());
  }

  public static boolean defaultsSetBandwidthLevel(tmedia_bandwidth_level_t bl) {
    return tinyWRAPJNI.MediaSessionMgr_defaultsSetBandwidthLevel(bl.swigValue());
  }

  public static tmedia_bandwidth_level_t defaultsGetBandwidthLevel() {
    return tmedia_bandwidth_level_t.swigToEnum(tinyWRAPJNI.MediaSessionMgr_defaultsGetBandwidthLevel());
  }

  public static boolean defaultsSetEchoTail(long echo_tail) {
    return tinyWRAPJNI.MediaSessionMgr_defaultsSetEchoTail(echo_tail);
  }

  public static long defaultsGetEchoTail() {
    return tinyWRAPJNI.MediaSessionMgr_defaultsGetEchoTail();
  }

  public static boolean defaultsSetEchoSkew(long echo_skew) {
    return tinyWRAPJNI.MediaSessionMgr_defaultsSetEchoSkew(echo_skew);
  }

  public static boolean defaultsSetEchoSuppEnabled(boolean echo_supp_enabled) {
    return tinyWRAPJNI.MediaSessionMgr_defaultsSetEchoSuppEnabled(echo_supp_enabled);
  }

  public static boolean defaultsGetEchoSuppEnabled() {
    return tinyWRAPJNI.MediaSessionMgr_defaultsGetEchoSuppEnabled();
  }

  public static boolean defaultsSetAgcEnabled(boolean agc_enabled) {
    return tinyWRAPJNI.MediaSessionMgr_defaultsSetAgcEnabled(agc_enabled);
  }

  public static boolean defaultsGetAgcEnabled() {
    return tinyWRAPJNI.MediaSessionMgr_defaultsGetAgcEnabled();
  }

  public static boolean defaultsSetAgcLevel(float agc_level) {
    return tinyWRAPJNI.MediaSessionMgr_defaultsSetAgcLevel(agc_level);
  }

  public static float defaultsGetAgcLevel() {
    return tinyWRAPJNI.MediaSessionMgr_defaultsGetAgcLevel();
  }

  public static boolean defaultsSetVadEnabled(boolean vad_enabled) {
    return tinyWRAPJNI.MediaSessionMgr_defaultsSetVadEnabled(vad_enabled);
  }

  public static boolean defaultsGetGetVadEnabled() {
    return tinyWRAPJNI.MediaSessionMgr_defaultsGetGetVadEnabled();
  }

  public static boolean defaultsSetNoiseSuppEnabled(boolean noise_supp_enabled) {
    return tinyWRAPJNI.MediaSessionMgr_defaultsSetNoiseSuppEnabled(noise_supp_enabled);
  }

  public static boolean defaultsGetNoiseSuppEnabled() {
    return tinyWRAPJNI.MediaSessionMgr_defaultsGetNoiseSuppEnabled();
  }

  public static boolean defaultsSetNoiseSuppLevel(int noise_supp_level) {
    return tinyWRAPJNI.MediaSessionMgr_defaultsSetNoiseSuppLevel(noise_supp_level);
  }

  public static int defaultsGetNoiseSuppLevel() {
    return tinyWRAPJNI.MediaSessionMgr_defaultsGetNoiseSuppLevel();
  }

  public static boolean defaultsSet100relEnabled(boolean _100rel_enabled) {
    return tinyWRAPJNI.MediaSessionMgr_defaultsSet100relEnabled(_100rel_enabled);
  }

  public static boolean defaultsGet100relEnabled() {
    return tinyWRAPJNI.MediaSessionMgr_defaultsGet100relEnabled();
  }

  public static boolean defaultsSetScreenSize(int sx, int sy) {
    return tinyWRAPJNI.MediaSessionMgr_defaultsSetScreenSize(sx, sy);
  }

  public static boolean defaultsSetAudioGain(int producer_gain, int consumer_gain) {
    return tinyWRAPJNI.MediaSessionMgr_defaultsSetAudioGain(producer_gain, consumer_gain);
  }

  public static boolean defaultsSetRtpPortRange(int range_start, int range_stop) {
    return tinyWRAPJNI.MediaSessionMgr_defaultsSetRtpPortRange(range_start, range_stop);
  }

}
