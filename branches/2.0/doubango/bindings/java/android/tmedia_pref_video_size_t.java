/* ----------------------------------------------------------------------------
 * This file was automatically generated by SWIG (http://www.swig.org).
 * Version 2.0.9
 *
 * Do not make changes to this file unless you know what you are doing--modify
 * the SWIG interface file instead.
 * ----------------------------------------------------------------------------- */

package org.doubango.tinyWRAP;

public enum tmedia_pref_video_size_t {
  tmedia_pref_video_size_sqcif,
  tmedia_pref_video_size_qcif,
  tmedia_pref_video_size_qvga,
  tmedia_pref_video_size_cif,
  tmedia_pref_video_size_hvga,
  tmedia_pref_video_size_vga,
  tmedia_pref_video_size_4cif,
  tmedia_pref_video_size_svga,
  tmedia_pref_video_size_480p,
  tmedia_pref_video_size_720p,
  tmedia_pref_video_size_16cif,
  tmedia_pref_video_size_1080p;

  public final int swigValue() {
    return swigValue;
  }

  public static tmedia_pref_video_size_t swigToEnum(int swigValue) {
    tmedia_pref_video_size_t[] swigValues = tmedia_pref_video_size_t.class.getEnumConstants();
    if (swigValue < swigValues.length && swigValue >= 0 && swigValues[swigValue].swigValue == swigValue)
      return swigValues[swigValue];
    for (tmedia_pref_video_size_t swigEnum : swigValues)
      if (swigEnum.swigValue == swigValue)
        return swigEnum;
    throw new IllegalArgumentException("No enum " + tmedia_pref_video_size_t.class + " with value " + swigValue);
  }

  @SuppressWarnings("unused")
  private tmedia_pref_video_size_t() {
    this.swigValue = SwigNext.next++;
  }

  @SuppressWarnings("unused")
  private tmedia_pref_video_size_t(int swigValue) {
    this.swigValue = swigValue;
    SwigNext.next = swigValue+1;
  }

  @SuppressWarnings("unused")
  private tmedia_pref_video_size_t(tmedia_pref_video_size_t swigEnum) {
    this.swigValue = swigEnum.swigValue;
    SwigNext.next = this.swigValue+1;
  }

  private final int swigValue;

  private static class SwigNext {
    private static int next = 0;
  }
}

