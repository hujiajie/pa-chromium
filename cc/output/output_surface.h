// Copyright 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CC_OUTPUT_OUTPUT_SURFACE_H_
#define CC_OUTPUT_OUTPUT_SURFACE_H_

#include "base/basictypes.h"
#include "base/memory/ref_counted.h"
#include "base/memory/scoped_ptr.h"
#include "base/memory/weak_ptr.h"
#include "cc/base/cc_export.h"
#include "cc/output/context_provider.h"
#include "cc/output/software_output_device.h"
#include "third_party/WebKit/public/platform/WebGraphicsContext3D.h"

namespace ui { struct LatencyInfo; }

namespace gfx {
class Rect;
class Size;
}

namespace cc {

class CompositorFrame;
class OutputSurfaceClient;
class OutputSurfaceCallbacks;

// Represents the output surface for a compositor. The compositor owns
// and manages its destruction. Its lifetime is:
//   1. Created on the main thread by the LayerTreeHost through its client.
//   2. Passed to the compositor thread and bound to a client via BindToClient.
//      From here on, it will only be used on the compositor thread.
//   3. If the 3D context is lost, then the compositor will delete the output
//      surface (on the compositor thread) and go back to step 1.
class CC_EXPORT OutputSurface {
 public:
  explicit OutputSurface(scoped_ptr<WebKit::WebGraphicsContext3D> context3d);

  explicit OutputSurface(scoped_ptr<cc::SoftwareOutputDevice> software_device);

  OutputSurface(scoped_ptr<WebKit::WebGraphicsContext3D> context3d,
                scoped_ptr<cc::SoftwareOutputDevice> software_device);

  virtual ~OutputSurface();

  struct Capabilities {
    Capabilities()
        : delegated_rendering(false),
          max_frames_pending(0),
          deferred_gl_initialization(false) {}

    bool delegated_rendering;
    int max_frames_pending;
    bool deferred_gl_initialization;
  };

  const Capabilities& capabilities() const {
    return capabilities_;
  }

  // Obtain the 3d context or the software device associated with this output
  // surface. Either of these may return a null pointer, but not both.
  // In the event of a lost context, the entire output surface should be
  // recreated.
  WebKit::WebGraphicsContext3D* context3d() const {
    return context3d_.get();
  }

  SoftwareOutputDevice* software_device() const {
    return software_device_.get();
  }

  // In the case where both the context3d and software_device are present
  // (namely Android WebView), this is called to determine whether the software
  // device should be used on the current frame.
  virtual bool ForcedDrawToSoftwareDevice() const;

  // Called by the compositor on the compositor thread. This is a place where
  // thread-specific data for the output surface can be initialized, since from
  // this point on the output surface will only be used on the compositor
  // thread.
  virtual bool BindToClient(OutputSurfaceClient* client);

  virtual void EnsureBackbuffer();
  virtual void DiscardBackbuffer();

  virtual void Reshape(gfx::Size size, float scale_factor);
  virtual gfx::Size SurfaceSize() const;

  virtual void BindFramebuffer();

  // The implementation may destroy or steal the contents of the CompositorFrame
  // passed in (though it will not take ownership of the CompositorFrame
  // itself).
  virtual void SwapBuffers(CompositorFrame* frame);

  // Notifies frame-rate smoothness preference. If true, all non-critical
  // processing should be stopped, or lowered in priority.
  virtual void UpdateSmoothnessTakesPriority(bool prefer_smoothness) {}

  // Requests a BeginFrame notification from the output surface. The
  // notification will be delivered by calling
  // OutputSurfaceClient::BeginFrame until the callback is disabled.
  virtual void SetNeedsBeginFrame(bool enable) {}

 protected:
  // Synchronously initialize context3d and enter hardware mode.
  // This can only supported in threaded compositing mode.
  // |offscreen_context_provider| should match what is returned by
  // LayerTreeClient::OffscreenContextProviderForCompositorThread.
  bool InitializeAndSetContext3D(
      scoped_ptr<WebKit::WebGraphicsContext3D> context3d,
      scoped_refptr<ContextProvider> offscreen_context_provider);

  void PostSwapBuffersComplete();

  OutputSurfaceClient* client_;
  struct cc::OutputSurface::Capabilities capabilities_;
  scoped_ptr<OutputSurfaceCallbacks> callbacks_;
  scoped_ptr<WebKit::WebGraphicsContext3D> context3d_;
  scoped_ptr<cc::SoftwareOutputDevice> software_device_;
  bool has_gl_discard_backbuffer_;
  bool has_swap_buffers_complete_callback_;
  gfx::Size surface_size_;
  float device_scale_factor_;

 private:
  void SetContext3D(scoped_ptr<WebKit::WebGraphicsContext3D> context3d);
  void SwapBuffersComplete();

  base::WeakPtrFactory<OutputSurface> weak_ptr_factory_;

  DISALLOW_COPY_AND_ASSIGN(OutputSurface);
};

}  // namespace cc

#endif  // CC_OUTPUT_OUTPUT_SURFACE_H_
