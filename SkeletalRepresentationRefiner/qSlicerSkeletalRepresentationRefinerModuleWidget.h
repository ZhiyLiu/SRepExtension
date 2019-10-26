/*==============================================================================

  Program: 3D Slicer

  Portions (c) Copyright Brigham and Women's Hospital (BWH) All Rights Reserved.

  See COPYRIGHT.txt
  or http://www.slicer.org/copyright/copyright.txt for details.

  Unless required by applicable law or agreed to in writing, software
  distributed under the License is distributed on an "AS IS" BASIS,
  WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
  See the License for the specific language governing permissions and
  limitations under the License.

==============================================================================*/

#ifndef __qSlicerSkeletalRepresentationRefinerModuleWidget_h
#define __qSlicerSkeletalRepresentationRefinerModuleWidget_h

// SlicerQt includes
#include "qSlicerAbstractModuleWidget.h"

#include "qSlicerSkeletalRepresentationRefinerModuleExport.h"

class qSlicerSkeletalRepresentationRefinerModuleWidgetPrivate;
class vtkMRMLNode;

/// \ingroup Slicer_QtModules_ExtensionTemplate
class Q_SLICER_QTMODULES_SKELETALREPRESENTATIONREFINER_EXPORT qSlicerSkeletalRepresentationRefinerModuleWidget :
  public qSlicerAbstractModuleWidget
{
  Q_OBJECT

public:

  typedef qSlicerAbstractModuleWidget Superclass;
  qSlicerSkeletalRepresentationRefinerModuleWidget(QWidget *parent=nullptr);
  virtual ~qSlicerSkeletalRepresentationRefinerModuleWidget();

public slots:
  // select image
  void SelectImage();
  // select srep model
  void SelectSrep();
  // select output path
  void SelectOutputPath();
  // start refinement
  void StartRefinement();
  // interpolate
  void StartInterpolate();
  // generate anti-aliased image from surfacemesh
  void GenerateImage();
  // transform srep into unit cube
  void TransformSrep();

  // show initial implied boundary
  void showImpliedBoundary();

  // show heat map of difference on the boundary
  void showBoundaryDiff();

protected:
  QScopedPointer<qSlicerSkeletalRepresentationRefinerModuleWidgetPrivate> d_ptr;

  virtual void setup();

private:
  Q_DECLARE_PRIVATE(qSlicerSkeletalRepresentationRefinerModuleWidget);
  Q_DISABLE_COPY(qSlicerSkeletalRepresentationRefinerModuleWidget);
};

#endif
