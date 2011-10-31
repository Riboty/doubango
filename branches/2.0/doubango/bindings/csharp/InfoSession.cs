/* ----------------------------------------------------------------------------
 * This file was automatically generated by SWIG (http://www.swig.org).
 * Version 1.3.39
 *
 * Do not make changes to this file unless you know what you are doing--modify
 * the SWIG interface file instead.
 * ----------------------------------------------------------------------------- */

namespace org.doubango.tinyWRAP {

using System;
using System.Runtime.InteropServices;

public class InfoSession : SipSession {
  private HandleRef swigCPtr;

  internal InfoSession(IntPtr cPtr, bool cMemoryOwn) : base(tinyWRAPPINVOKE.InfoSessionUpcast(cPtr), cMemoryOwn) {
    swigCPtr = new HandleRef(this, cPtr);
  }

  internal static HandleRef getCPtr(InfoSession obj) {
    return (obj == null) ? new HandleRef(null, IntPtr.Zero) : obj.swigCPtr;
  }

  ~InfoSession() {
    Dispose();
  }

  public override void Dispose() {
    lock(this) {
      if(swigCPtr.Handle != IntPtr.Zero && swigCMemOwn) {
        swigCMemOwn = false;
        tinyWRAPPINVOKE.delete_InfoSession(swigCPtr);
      }
      swigCPtr = new HandleRef(null, IntPtr.Zero);
      GC.SuppressFinalize(this);
      base.Dispose();
    }
  }

  public InfoSession(SipStack pStack) : this(tinyWRAPPINVOKE.new_InfoSession(SipStack.getCPtr(pStack)), true) {
  }

  public bool send(byte[] payload, uint len, ActionConfig config) {
    bool ret = tinyWRAPPINVOKE.InfoSession_send__SWIG_0(swigCPtr, payload, len, ActionConfig.getCPtr(config));
    return ret;
  }

  public bool send(byte[] payload, uint len) {
    bool ret = tinyWRAPPINVOKE.InfoSession_send__SWIG_1(swigCPtr, payload, len);
    return ret;
  }

  public bool accept(ActionConfig config) {
    bool ret = tinyWRAPPINVOKE.InfoSession_accept__SWIG_0(swigCPtr, ActionConfig.getCPtr(config));
    return ret;
  }

  public bool accept() {
    bool ret = tinyWRAPPINVOKE.InfoSession_accept__SWIG_1(swigCPtr);
    return ret;
  }

  public bool reject(ActionConfig config) {
    bool ret = tinyWRAPPINVOKE.InfoSession_reject__SWIG_0(swigCPtr, ActionConfig.getCPtr(config));
    return ret;
  }

  public bool reject() {
    bool ret = tinyWRAPPINVOKE.InfoSession_reject__SWIG_1(swigCPtr);
    return ret;
  }

}

}
