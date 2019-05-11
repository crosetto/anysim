//
// Created by egi on 5/11/19.
//

#ifndef FDTD_OPENGL_WIDGET_H
#define FDTD_OPENGL_WIDGET_H

#include <QOpenGLWidget>
#include <QOpenGLFunctions>
#include <QOpenGLShaderProgram>

#include <memory>

class opengl_widget : public QOpenGLWidget, protected QOpenGLFunctions
{
  Q_OBJECT;

public:
  opengl_widget () = delete;
  opengl_widget (unsigned int nx, unsigned int ny, float x_size, float y_size);
  ~opengl_widget () override;

  GLfloat *get_colors ();

public slots:
  void update_colors ();

protected:
  void initializeGL () override;
  void resizeGL (int width, int height) override;
  void paintGL () override;

private:
  std::unique_ptr<QOpenGLShaderProgram> program;
  GLint attribute_coord2d, attribute_v_color;
  GLuint vbo_vertices, vbo_colors;

  bool initialized = false;
  const long int elements_count;
  const int vertices_per_element = 4;
  const int coords_per_vertex = 2;
  const int vertex_data_per_element = vertices_per_element * coords_per_vertex;
  const int colors_per_vertex = 3;
  const int color_data_per_element = colors_per_vertex * vertices_per_element;

  std::unique_ptr<GLfloat[]> colors;
  std::unique_ptr<GLfloat[]> vertices;
};


#endif //FDTD_OPENGL_WIDGET_H