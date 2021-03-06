#include "EncoderManager.h"
#include "MTLContext.h"
#include "sun_java2d_SunGraphics2D.h"
#import "common.h"

// NOTE: uncomment to disable comparing cached encoder states with requested (for debugging)
// #define ALWAYS_UPDATE_ENCODER_STATES

const SurfaceRasterFlags defaultRasterFlags = { JNI_FALSE, JNI_TRUE };

// Internal utility class that represents the set of 'mutable' encoder properties
@interface EncoderStates : NSObject
@property (readonly) MTLClip * clip;

- (id)init;
- (void)dealloc;

- (void)reset:(id<MTLTexture>)destination
           isDstOpaque:(jboolean)isDstOpaque
    isDstPremultiplied:(jboolean)isDstPremultiplied
                  isAA:(jboolean)isAA;

- (void)updateEncoder:(id<MTLRenderCommandEncoder>)encoder
                paint:(MTLPaint *)paint
            composite:(MTLComposite *)composite
            isTexture:(jboolean)isTexture
                 isAA:(jboolean)isAA
             srcFlags:(const SurfaceRasterFlags * _Nullable)srcFlags
                 clip:(MTLClip *)clip
            transform:(MTLTransform *)transform
          forceUpdate:(jboolean)forceUpdate;
@property jboolean aa;
@end

@implementation EncoderStates {
    MTLPipelineStatesStorage * _pipelineStateStorage;
    id<MTLDevice> _device;

    // Persistent encoder properties
    id<MTLTexture> _destination;
    SurfaceRasterFlags _dstFlags;

    jboolean _isAA;

    //
    // Cached 'mutable' states of encoder
    //

    // Composite rule and source raster flags (it affects the CAD-multipliers (of pipelineState))
    MTLComposite * _composite;
    SurfaceRasterFlags _srcFlags;

    // Paint mode (it affects shaders (of pipelineState) and corresponding buffers)
    MTLPaint * _paint;

    // If true, indicates that encoder is used for texture drawing (user must do [encoder setFragmentTexture:] before drawing)
    jboolean _isTexture;
    int _interpolationMode;

    // Clip rect or stencil
    MTLClip * _clip;

    // Transform (affects transformation inside vertex shader)
    MTLTransform * _transform;
}
@synthesize aa = _isAA;

- (id)init {
    self = [super init];
    if (self) {
        _destination = nil;
        _composite = [[MTLComposite alloc] init];
        _paint = [[MTLPaint alloc] init];
        _transform = [[MTLTransform alloc] init];
        _clip = [[MTLClip alloc] init];
    }
    return self;
}

- (void)dealloc {
    [_composite release];
    [_paint release];
    [_transform release];
    [super dealloc];
}

- (void)setContext:(MTLContext * _Nonnull)mtlc {
    self->_pipelineStateStorage = mtlc.pipelineStateStorage;
    self->_device = mtlc.device;
}

- (void)reset:(id<MTLTexture>)destination
           isDstOpaque:(jboolean)isDstOpaque
    isDstPremultiplied:(jboolean)isDstPremultiplied
                  isAA:(jboolean)isAA {
    _destination = destination;
    _dstFlags.isOpaque = isDstOpaque;
    _dstFlags.isPremultiplied = isDstPremultiplied;
    _isAA = isAA;
    // NOTE: probably it's better to invalidate/reset all cached states now
}

- (void)updateEncoder:(id<MTLRenderCommandEncoder>)encoder
                paint:(MTLPaint *)paint
            composite:(MTLComposite *)composite
            isTexture:(jboolean)isTexture
        interpolation:(int)interpolation
                 isAA:(jboolean)isAA
             srcFlags:(const SurfaceRasterFlags * _Nullable)srcFlags
                 clip:(MTLClip *)clip
            transform:(MTLTransform *)transform
          forceUpdate:(jboolean)forceUpdate
{
    // 1. Process special case for stencil mask generation
    if (clip.stencilMaskGenerationInProgress == JNI_TRUE) {
        // use separate pipeline state for stencil generation
        if (forceUpdate || (_clip.stencilMaskGenerationInProgress != JNI_TRUE)) {
            [_clip copyFrom:clip];
            [_clip setMaskGenerationPipelineState:encoder
                                        destWidth:_destination.width
                                       destHeight:_destination.height
                             pipelineStateStorage:_pipelineStateStorage];
        }

        [self updateTransform:encoder transform:transform forceUpdate:forceUpdate];
        return;
    }

    // 2. Otherwise update all 'mutable' properties of encoder
    [self updatePipelineState:encoder
                        paint:paint
                    composite:composite
                isStencilUsed:[clip isShape]
                    isTexture:isTexture
                interpolation:interpolation
                         isAA:isAA
                     srcFlags:srcFlags
                  forceUpdate:forceUpdate];
    [self updateTransform:encoder transform:transform forceUpdate:forceUpdate];
    [self updateClip:encoder clip:clip forceUpdate:forceUpdate];
}

//
// Internal methods that update states when necessary (compare with cached states)
//

// Updates pipelineState (and corresponding buffers) with use of paint+composite+flags
- (void)updatePipelineState:(id<MTLRenderCommandEncoder>)encoder
                      paint:(MTLPaint *)paint
                  composite:(MTLComposite *)composite
              isStencilUsed:(jboolean)isStencilUsed
                  isTexture:(jboolean)isTexture
              interpolation:(int)interpolation
                       isAA:(jboolean)isAA
                   srcFlags:(const SurfaceRasterFlags * _Nullable)srcFlags
                forceUpdate:(jboolean)forceUpdate
{
    if (srcFlags == NULL)
        srcFlags = &defaultRasterFlags;

    if (!forceUpdate
        && [_paint isEqual:paint]
        && [_composite isEqual:composite]
        && (_isTexture == isTexture && (!isTexture || _interpolationMode == interpolation)) // interpolation is used only in texture mode
        && _isAA == isAA
        && _srcFlags.isOpaque == srcFlags->isOpaque && _srcFlags.isPremultiplied == srcFlags->isPremultiplied)
        return;

    [_paint copyFrom:paint];
    [_composite copyFrom:composite];
    _isTexture = isTexture;
    _interpolationMode = interpolation;
    _isAA = isAA;
    _srcFlags = *srcFlags;

    if ((jint)[composite getCompositeState] == sun_java2d_SunGraphics2D_COMP_XOR) {
        [paint setXorModePipelineState:encoder
                      composite:_composite
                  isStencilUsed:isStencilUsed
                      isTexture:_isTexture
                  interpolation:interpolation
                       srcFlags:&_srcFlags
                       dstFlags:&_dstFlags
           pipelineStateStorage:_pipelineStateStorage];
    } else {
        [paint setPipelineState:encoder
                      composite:_composite
                  isStencilUsed:isStencilUsed
                      isTexture:_isTexture
                  interpolation:interpolation
                           isAA:isAA
                       srcFlags:&_srcFlags
                       dstFlags:&_dstFlags
           pipelineStateStorage:_pipelineStateStorage];
    }
}

- (void) updateClip:(id<MTLRenderCommandEncoder>)encoder clip:(MTLClip *)clip forceUpdate:(jboolean)forceUpdate
{
    if (clip.stencilMaskGenerationInProgress == JNI_TRUE) {
        // don't set setScissorOrStencil when generateion in progress
        return;
    }

    if (!forceUpdate && [_clip isEqual:clip])
        return;

    [_clip copyFrom:clip];
    [_clip setScissorOrStencil:encoder
                     destWidth:_destination.width
                    destHeight:_destination.height
                        device:_device];
}

- (void)updateTransform:(id <MTLRenderCommandEncoder>)encoder
              transform:(MTLTransform *)transform
            forceUpdate:(jboolean)forceUpdate
{
    if (!forceUpdate
        && [_transform isEqual:transform])
        return;

    [_transform copyFrom:transform];
    [_transform setVertexMatrix:encoder
                        destWidth:_destination.width
                       destHeight:_destination.height];
}

@end

@implementation EncoderManager {
    MTLContext * _mtlc; // used to obtain CommandBufferWrapper and Composite/Paint/Transform

    id<MTLRenderCommandEncoder> _encoder;

    // 'Persistent' properties of encoder
    id<MTLTexture> _destination;
    id<MTLTexture> _aaDestination;
    BOOL _useStencil;

    // 'Mutable' states of encoder
    EncoderStates * _encoderStates;
}

- (id _Nonnull)init {
    self = [super init];
    if (self) {
        _encoder = nil;
        _destination = nil;
        _aaDestination = nil;
        _useStencil = NO;
        _encoderStates = [[EncoderStates alloc] init];

    }
    return self;
}

- (void)dealloc {
    [_encoderStates release];
    [super dealloc];
}

- (void)setContext:(MTLContex * _Nonnull)mtlc {
    self->_mtlc = mtlc;
    [self->_encoderStates setContext:mtlc];
}

- (id<MTLRenderCommandEncoder> _Nonnull) getRenderEncoder:(const BMTLSDOps * _Nonnull)dstOps
{
    return [self getRenderEncoder:dstOps->pTexture isDstOpaque:dstOps->isOpaque];
}

- (id<MTLRenderCommandEncoder> _Nonnull)getAARenderEncoder:(const BMTLSDOps * _Nonnull)dstOps {
  id<MTLTexture> dstTxt = dstOps->pTexture;
  return [self getEncoder:dstTxt
                 isOpaque:dstOps->isOpaque
                isTexture:JNI_FALSE
           interpolation:INTERPOLATION_NEAREST_NEIGHBOR
                     isAA:JNI_TRUE
                 srcFlags:NULL];
}

- (id<MTLRenderCommandEncoder> _Nonnull)getRenderEncoder:(id<MTLTexture> _Nonnull)dest
                                             isDstOpaque:(bool)isOpaque
{
    return [self getEncoder:dest
                 isOpaque:isOpaque
                isTexture:JNI_FALSE
            interpolation:INTERPOLATION_NEAREST_NEIGHBOR
                     isAA:JNI_FALSE
                 srcFlags:NULL];
}

- (id<MTLRenderCommandEncoder> _Nonnull) getTextureEncoder:(const BMTLSDOps * _Nonnull)dstOps
                                      isSrcOpaque:(bool)isSrcOpaque
{
    return [self getTextureEncoder:dstOps->pTexture
                       isSrcOpaque:isSrcOpaque
                       isDstOpaque:dstOps->isOpaque
                     interpolation:INTERPOLATION_NEAREST_NEIGHBOR];
}

- (id<MTLRenderCommandEncoder> _Nonnull) getTextureEncoder:(id<MTLTexture> _Nonnull)dest
                                               isSrcOpaque:(bool)isSrcOpaque
                                               isDstOpaque:(bool)isDstOpaque
{
    return [self getTextureEncoder:dest
                       isSrcOpaque:isSrcOpaque
                       isDstOpaque:isDstOpaque
                     interpolation:INTERPOLATION_NEAREST_NEIGHBOR
                              isAA:JNI_FALSE];
}

- (id<MTLRenderCommandEncoder> _Nonnull) getTextureEncoder:(id<MTLTexture> _Nonnull)dest
                                      isSrcOpaque:(bool)isSrcOpaque
                                      isDstOpaque:(bool)isDstOpaque
                                    interpolation:(int)interpolation
                                             isAA:(jboolean)isAA
{
    SurfaceRasterFlags srcFlags = { isSrcOpaque, JNI_TRUE };
    return [self getEncoder:dest
                   isOpaque:isDstOpaque
                  isTexture:JNI_TRUE
              interpolation:interpolation
                       isAA:isAA
                   srcFlags:&srcFlags];
}

- (id<MTLRenderCommandEncoder> _Nonnull) getTextureEncoder:(id<MTLTexture> _Nonnull)dest
                                               isSrcOpaque:(bool)isSrcOpaque
                                               isDstOpaque:(bool)isDstOpaque
                                             interpolation:(int)interpolation
{
    return [self getTextureEncoder:dest isSrcOpaque:isSrcOpaque isDstOpaque:isDstOpaque interpolation:interpolation isAA:JNI_FALSE];
}

- (id<MTLRenderCommandEncoder> _Nonnull)
    getEncoder:(id<MTLTexture> _Nonnull)dest
      isOpaque:(jboolean)isOpaque
     isTexture:(jboolean)isTexture
 interpolation:(int)interpolation
          isAA:(jboolean)isAA
      srcFlags:(const SurfaceRasterFlags *_Nullable)srcFlags {
  //
  // 1. check whether it's necessary to call endEncoder
  //
  jboolean needEnd = JNI_FALSE;
  if (_encoder != nil) {
    if (_destination != dest || isAA != _encoderStates.aa) {
      J2dTraceLn2(J2D_TRACE_VERBOSE,
                  "end common encoder because of dest change: %p -> %p",
                  _destination, dest);
      needEnd = JNI_TRUE;
    } else if ((_useStencil == NO) != ([_mtlc.clip isShape] == NO)) {
      // 1. When mode changes RECT -> SHAPE we must recreate encoder with
      // stencilAttachment (todo: consider the case when current encoder already
      // has stencil)
      //
      // 2. When mode changes SHAPE -> RECT it seems that we can use the same
      // encoder with disabled stencil test, but [encoder
      // setDepthStencilState:nil] causes crash, so we have to recreate encoder
      // in such case
      J2dTraceLn2(J2D_TRACE_VERBOSE,
                  "end common encoder because toggle stencil: %d -> %d",
                  (int)_useStencil, (int)[_mtlc.clip isShape]);
      needEnd = JNI_TRUE;
    }
  }
  if (needEnd)
    [self endEncoder];

  //
  // 2. recreate encoder if necessary
  //
  jboolean forceUpdate = JNI_FALSE;
#ifdef ALWAYS_UPDATE_ENCODER_STATES
  forceUpdate = JNI_TRUE;
#endif // ALWAYS_UPDATE_ENCODER_STATES

  if (_encoder == nil) {
    _destination = dest;
    _useStencil = [_mtlc.clip isShape];
    forceUpdate = JNI_TRUE;

    MTLCommandBufferWrapper *cbw = [_mtlc getCommandBufferWrapper];
    MTLRenderPassDescriptor *rpd =
        [MTLRenderPassDescriptor renderPassDescriptor];
    MTLRenderPassColorAttachmentDescriptor *ca = rpd.colorAttachments[0];
    if (isAA && !isTexture) {
      MTLTexturePoolItem *tiBuf = [_mtlc.texturePool getTexture:dest.width
                                                      height:dest.height
                                                      format:MTLPixelFormatBGRA8Unorm];
      [cbw registerPooledTexture:tiBuf];
      [tiBuf release];
      _aaDestination = tiBuf.texture;

      MTLTexturePoolItem *ti = [_mtlc.texturePool getTexture:dest.width
                                                      height:dest.height
                                                      format:_aaDestination.pixelFormat
                                               isMultiSample:YES];
      [cbw registerPooledTexture:ti];
      [ti release];
      ca.texture = ti.texture;
      ca.resolveTexture = _aaDestination;
      ca.clearColor = MTLClearColorMake(0.0f, 0.0f, 0.0f, 0.0f);
      ca.loadAction = MTLLoadActionClear;
      ca.storeAction = MTLStoreActionMultisampleResolve;
    } else {
      ca.texture = dest;
      ca.loadAction = MTLLoadActionLoad;
      ca.storeAction = MTLStoreActionStore;
    }

    if (_useStencil) {
      // If you enable stencil testing or stencil writing, the
      // MTLRenderPassDescriptor must include a stencil attachment.
      rpd.stencilAttachment.texture = _mtlc.clip.stencilTextureRef;
      rpd.stencilAttachment.loadAction = MTLLoadActionLoad;
      rpd.stencilAttachment.storeAction = MTLStoreActionDontCare;
    }

    // J2dTraceLn1(J2D_TRACE_VERBOSE, "created render encoder to draw on
    // tex=%p", dest);
    _encoder = [[cbw getCommandBuffer] renderCommandEncoderWithDescriptor:rpd];
    [rpd release];

    [_encoderStates reset:dest
               isDstOpaque:isOpaque
        isDstPremultiplied:YES
                      isAA:isAA];
  }

  //
  // 3. update encoder states
  //
  [_encoderStates updateEncoder:_encoder
                          paint:_mtlc.paint
                      composite:_mtlc.composite
                      isTexture:isTexture
                  interpolation:interpolation
                           isAA:isAA
                       srcFlags:srcFlags
                           clip:_mtlc.clip
                      transform:_mtlc.transform
                    forceUpdate:forceUpdate];

  return _encoder;
}

- (id<MTLBlitCommandEncoder> _Nonnull) createBlitEncoder {
    [self endEncoder];
    return [[[_mtlc getCommandBufferWrapper] getCommandBuffer] blitCommandEncoder];
}

- (void) endEncoder {
    if (_encoder != nil) {
      [_encoder endEncoding];
      [_encoder release];
      _encoder = nil;
        if (_aaDestination != nil) {
          id<MTLTexture> aaDest = _aaDestination;
          _aaDestination = nil;
          NSUInteger _w = _destination.width;
          NSUInteger _h = _destination.height;
          _encoder = [self getTextureEncoder:_destination
                                 isSrcOpaque:JNI_FALSE
                                 isDstOpaque:JNI_TRUE
                               interpolation:INTERPOLATION_NEAREST_NEIGHBOR
                                        isAA:JNI_TRUE];

          struct TxtVertex quadTxVerticesBuffer[] = {
              {{0, 0}, {0, 0}},
              {{0,_h}, {0, 1}},
              {{_w, 0},{1, 0}},
              {{0, _h},{0, 1}},
              {{_w, _h}, {1, 1}},
              {{_w, 0}, {1, 0}}
          };

          [_encoder setVertexBytes:quadTxVerticesBuffer length:sizeof(quadTxVerticesBuffer) atIndex:MeshVertexBuffer];
          [_encoder setFragmentTexture:aaDest atIndex: 0];
          [_encoder drawPrimitives:MTLPrimitiveTypeTriangle vertexStart:0 vertexCount:6];
          [_encoder endEncoding];
          [_encoder release];
        }

        _encoder = nil;
        _destination = nil;
    }
}

@end
