/******************************************************************************
    QtAV:  Media play library based on Qt and FFmpeg
    Copyright (C) 2014 Wang Bin <wbsecg1@gmail.com>

*   This file is part of QtAV

    This library is free software; you can redistribute it and/or
    modify it under the terms of the GNU Lesser General Public
    License as published by the Free Software Foundation; either
    version 2.1 of the License, or (at your option) any later version.

    This library is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
    Lesser General Public License for more details.

    You should have received a copy of the GNU Lesser General Public
    License along with this library; if not, write to the Free Software
    Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301  USA
******************************************************************************/

#include "QtAV/OpenGLVideo.h"
#if QT_VERSION >= QT_VERSION_CHECK(5, 0, 0)
#include <QtGui/QOpenGLShaderProgram>
#include <QtGui/QOpenGLFunctions>
#include <QtGui/QSurface>
#else
#include <QtOpenGL/QGLShaderProgram>
#include <QtOpenGL/QGLFunctions>
#define QOpenGLShaderProgram QGLShaderProgram
#define QOpenGLShader QGLShader
#define QOpenGLFunctions QGLFunctions
#endif
#include "QtAV/SurfaceInterop.h"
#include "QtAV/VideoShader.h"
#include "QtAV/private/ShaderManager.h"

namespace QtAV {

// FIXME: why crash if inherits both QObject and DPtrPrivate?
class OpenGLVideoPrivate : public DPtrPrivate<OpenGLVideo>
{
public:
    OpenGLVideoPrivate()
        : ctx(0)
        , manager(0)
        , material(new VideoMaterial())
    {}
    ~OpenGLVideoPrivate() {
        if (material) {
            delete material;
            material = 0;
        }
    }

    void resetGL() {
        ctx = 0;
        if (!manager)
            return;
        manager->setParent(0);
        delete manager;
        manager = 0;
        if (material) {
            delete material;
            material = new VideoMaterial();
        }
    }

public:
    QOpenGLContext *ctx;
    ShaderManager *manager;
    VideoMaterial *material;
    TexturedGeometry geometry;
    QRectF rect;
    QMatrix4x4 matrix;
};

OpenGLVideo::OpenGLVideo() {}

// TODO: set surface/device size here (viewport?)
void OpenGLVideo::setOpenGLContext(QOpenGLContext *ctx)
{
    DPTR_D(OpenGLVideo);
    if (!ctx) {
        d.resetGL();
        return;
    }
#if QT_VERSION >= QT_VERSION_CHECK(5, 0, 0)
    d.manager = ctx->findChild<ShaderManager*>(QStringLiteral("__qtav_shader_manager"));
    QSizeF surfaceSize = QOpenGLContext::currentContext()->surface()->size();
#else
    d.resetGL();
    QSizeF surfaceSize = QSizeF(ctx->device()->width(), ctx->device()->height());
#endif
    d.ctx = ctx; // Qt4: set to null in resetGL()
    setProjectionMatrixToRect(QRectF(QPointF(), surfaceSize));
    if (d.manager)
        return;
    // TODO: what if ctx is delete?
    d.manager = new ShaderManager(ctx);
    d.manager->setObjectName(QStringLiteral("__qtav_shader_manager"));
#if QT_VERSION >= QT_VERSION_CHECK(5, 0, 0)
    QObject::connect(ctx, SIGNAL(aboutToBeDestroyed()), this, SLOT(resetGL()), Qt::DirectConnection); //direct?
#endif
}

QOpenGLContext* OpenGLVideo::openGLContext()
{
    return d_func().ctx;
}

void OpenGLVideo::setCurrentFrame(const VideoFrame &frame)
{
    d_func().material->setCurrentFrame(frame);
}

void OpenGLVideo::setProjectionMatrixToRect(const QRectF &v)
{
    DPTR_D(OpenGLVideo);
    d.rect = v;
    d.matrix.setToIdentity();
    d.matrix.ortho(v);
    // Mirrored relative to the usual Qt coordinate system with origin in the top left corner.
    //mirrored = mat(0, 0) * mat(1, 1) - mat(0, 1) * mat(1, 0) > 0;
}

void OpenGLVideo::setProjectionMatrix(const QMatrix4x4 &matrix)
{
    d_func().matrix = matrix;
}

void OpenGLVideo::setBrightness(qreal value)
{
    d_func().material->setBrightness(value);
}

void OpenGLVideo::setContrast(qreal value)
{
    d_func().material->setContrast(value);
}

void OpenGLVideo::setHue(qreal value)
{
    d_func().material->setHue(value);
}

void OpenGLVideo::setSaturation(qreal value)
{
    d_func().material->setSaturation(value);
}

void OpenGLVideo::render(const QRectF &target, const QRectF& roi, const QMatrix4x4& transform)
{
    DPTR_D(OpenGLVideo);
    Q_ASSERT(d.manager);
    VideoShader *shader = d.manager->prepareMaterial(d.material);
    shader->update(d.material);
    shader->program()->setUniformValue(shader->matrixLocation(), transform*d.matrix);
    // uniform end. attribute begin
    if (target.isValid())
        d.geometry.setRect(target, d.material->normalizedROI(roi));
    else
        d.geometry.setRect(d.rect, d.material->normalizedROI(roi));

    // normalize?
#if 1
    shader->program()->setAttributeArray(0, GL_FLOAT, d.geometry.data(0), 2, d.geometry.stride());
    shader->program()->setAttributeArray(1, GL_FLOAT, d.geometry.data(1), 2, d.geometry.stride());
#else
#if QT_VERSION < QT_VERSION_CHECK(5, 0, 0)
    static QGLFunctions *glf = new QGLFunctions();
    glf->initializeGLFunctions();
#else
    QOpenGLFunctions *glf = QOpenGLContext::currentContext()->functions();
#endif
    glf->glVertexAttribPointer(0, 2, GL_FLOAT, GL_TRUE, d.geometry.stride(), d.geometry.data(0));
    glf->glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, d.geometry.stride(), d.geometry.data(1));
#endif
    char const *const *attr = shader->attributeNames();
    for (int i = 0; attr[i]; ++i) {
        shader->program()->enableAttributeArray(i); //TODO: in setActiveShader
    }

   glDrawArrays(d.geometry.mode(), 0, d.geometry.vertexCount());

   // d.shader->program()->release(); //glUseProgram(0)
   for (int i = 0; attr[i]; ++i) {
       shader->program()->disableAttributeArray(i); //TODO: in setActiveShader
   }

   d.material->unbind();
}

void OpenGLVideo::resetGL()
{
    qDebug("~~~~~~~~~resetGL %p. from sender %p", d_func().manager, sender());
    d_func().resetGL();
}

} //namespace QtAV
