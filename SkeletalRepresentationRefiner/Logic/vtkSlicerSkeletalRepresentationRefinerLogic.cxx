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

// SkeletalRepresentationRefiner Logic includes
#include "vtkSlicerSkeletalRepresentationRefinerLogic.h"
#include <stdlib.h>
// MRML includes
#include <vtkMRMLScene.h>
#include <vtkMRMLModelNode.h>
#include <vtkMRMLModelDisplayNode.h>
#include <vtkMRMLDisplayNode.h>
#include <vtkMRMLMarkupsDisplayNode.h>
#include <vtkMRMLMarkupsFiducialNode.h>
#include <vtkMRMLMarkupsNode.h>
#include "vtkSlicerMarkupsLogic.h"
// VTK includes
#include <vtkIntArray.h>
#include <vtkNew.h>
#include <vtkObjectFactory.h>

#include <vtkPoints.h>
#include <vtkLine.h>
#include <vtkQuad.h>
#include <vtkPolyData.h>
#include <vtkPointData.h>
#include <vtkDataArray.h>
#include <vtkDoubleArray.h>
#include <vtkExtractSurface.h>
#include <vtkImageData.h>
#include <vtkPCANormalEstimation.h>
#include <vtkParametricSpline.h>
#include <vtkCellLocator.h>
#include <vtksys/SystemTools.hxx>

#include <vtkPolyDataReader.h>
#include <vtkPolyDataWriter.h>
#include <vtkSignedDistance.h>
#include <vtkCleanPolyData.h>
#include <vtkPointSource.h>
#include <vtkCurvatures.h>
#include <vtkPointLocator.h>
#include <vtkAppendPolyData.h>
#include <vtkLookupTable.h>
#include <vtkParametricFunctionSource.h>
#include <vtkImplicitPolyDataDistance.h>
#include <vtkXMLPolyDataReader.h>
#include <vtkXMLPolyDataWriter.h>
#include <vtkXMLDataParser.h>
#include <vtkSurfaceReconstructionFilter.h>
#include <vtkColorTransferFunction.h>
#include <vtkProgrammableSource.h>
#include <vtkContourFilter.h>
#include <vtkDistancePolyDataFilter.h>
#include <vtkReverseSense.h>
#include "vtkSlicerSkeletalRepresentationInterpolater.h"
#include "vtkSrep.h"
#include "vtkSpoke.h"
#include "newuoa.h"
#include "vtkPolyData2ImageData.h"
#include "vtkApproximateSignedDistanceMap.h"
#include "vtkGradientDistanceFilter.h"
#include <vtkBoundingBox.h>
#include <vtkMRMLProceduralColorNode.h>
// STD includes
#include <cassert>
const double voxelSpacing = 0.005;
const std::string newFilePrefix = "/refined_";
//----------------------------------------------------------------------------
vtkStandardNewMacro(vtkSlicerSkeletalRepresentationRefinerLogic);

//----------------------------------------------------------------------------
vtkSlicerSkeletalRepresentationRefinerLogic::vtkSlicerSkeletalRepresentationRefinerLogic()
{
}

//----------------------------------------------------------------------------
vtkSlicerSkeletalRepresentationRefinerLogic::~vtkSlicerSkeletalRepresentationRefinerLogic()
{
}

//----------------------------------------------------------------------------
void vtkSlicerSkeletalRepresentationRefinerLogic::PrintSelf(ostream& os, vtkIndent indent)
{
    this->Superclass::PrintSelf(os, indent);
}

void vtkSlicerSkeletalRepresentationRefinerLogic::SetImageFileName(const std::string &imageFilePath)
{
    mTargetMeshFilePath = imageFilePath;
    // visualize the input surface mesh
    vtkSmartPointer<vtkPolyDataReader> reader = vtkSmartPointer<vtkPolyDataReader>::New();
    reader->SetFileName(imageFilePath.c_str());
    reader->Update();

    vtkSmartPointer<vtkPolyData> surface = reader->GetOutput();
    Visualize(surface, "Input surface mesh", 0, 0, 0);
}

void vtkSlicerSkeletalRepresentationRefinerLogic::SetSrepFileName(const std::string &srepFilePath)
{
    mSrepFilePath = srepFilePath;
    int nRows = 0, nCols = 0;
    double crestShift = 0.0;
    std::string up, down, crest;
    ParseHeader(srepFilePath, &nRows, &nCols, &crestShift, &up, &down, &crest);
    if(nRows == 0 || nCols == 0)
    {
        std::cerr << "The s-rep model is empty." << std::endl;
        return;
    }
    std::vector<double> up_radii, down_radii, up_dirs, down_dirs, up_skeletalPoints, down_skeletalPoints;
    Parse(up, mCoeffArray, up_radii, up_dirs, up_skeletalPoints);

    vtkSrep *srep = new vtkSrep(nRows, nCols, up_radii, up_dirs, up_skeletalPoints);
    if(srep->IsEmpty())
    {
        std::cerr << "The s-rep model is empty." << std::endl;
        delete srep;
        srep = nullptr;
        return;
    }
    vtkSmartPointer<vtkPolyData> upSrepPoly = vtkSmartPointer<vtkPolyData>::New();
    ConvertSpokes2PolyData(srep->GetAllSpokes(), upSrepPoly);
    Visualize(upSrepPoly, "up spokes", 0, 1, 1);

    Parse(down, mCoeffArray, down_radii, down_dirs, down_skeletalPoints);

    vtkSrep *downSrep = new vtkSrep(nRows, nCols, down_radii, down_dirs, down_skeletalPoints);
    if(downSrep->IsEmpty())
    {
        std::cerr << "The s-rep model is empty." << std::endl;
        delete downSrep;
        downSrep = nullptr;
        return;
    }
    vtkSmartPointer<vtkPolyData> downSrepPoly = vtkSmartPointer<vtkPolyData>::New();
    ConvertSpokes2PolyData(downSrep->GetAllSpokes(), downSrepPoly);
    Visualize(downSrepPoly, "down spokes", 1, 0, 0);

    std::vector<vtkSpoke*> crestSpokes, reorderedCrest;
    ParseCrest(crest, crestSpokes);

    vtkSmartPointer<vtkPolyData> crestSrepPoly = vtkSmartPointer<vtkPolyData>::New();
    ConvertSpokes2PolyData(crestSpokes, crestSrepPoly);
    Visualize(crestSrepPoly, "crest spokes", 0, 0, 1);

    // show fold curve
    vtkSmartPointer<vtkPoints> foldCurvePts = vtkSmartPointer<vtkPoints>::New();
    vtkSmartPointer<vtkCellArray> foldCurveCell = vtkSmartPointer<vtkCellArray>::New();
    ReorderCrestSpokes(nRows, nCols, crestSpokes, reorderedCrest);
    ConnectFoldCurve(reorderedCrest, foldCurvePts, foldCurveCell);
    vtkSmartPointer<vtkPolyData> foldPoly = vtkSmartPointer<vtkPolyData>::New();
    foldPoly->SetPoints(foldCurvePts);
    foldPoly->SetPolys(foldCurveCell);
    Visualize(foldPoly, "fold curve", 1, 1, 0);
}

void vtkSlicerSkeletalRepresentationRefinerLogic::SetOutputPath(const string &outputPath)
{
    mOutputPath = outputPath;
}

void vtkSlicerSkeletalRepresentationRefinerLogic::Refine(double stepSize, double endCriterion, int maxIter, int interpolationLevel)
{
    mFirstCost = true;
    // 1. parse file
    const std::string headerFileName = mSrepFilePath;
    int nRows = 0, nCols = 0;
    double crestShift = 0.0;
    std::string up, down, crest;
    ParseHeader(headerFileName, &nRows, &nCols, &crestShift, &up, &down, &crest);

    if(nRows == 0 || nCols == 0)
    {
        std::cerr << "The s-rep model is empty." << std::endl;
        return;
    }

    mNumCols = nCols;
    mNumRows = nRows;

    // Prepare signed distance image
    AntiAliasSignedDistanceMap(mTargetMeshFilePath);

    // Compute transformation matrix from srep to image coordinate system, namely, unit cube cs.
    TransformSrep(headerFileName);

    // make tuples of interpolation positions (u,v)
    mInterpolationLevel = interpolationLevel;
    mInterpolatePositions.clear();
    double tol = 1e-6;
    int shares = static_cast<int>(pow(2, interpolationLevel));
    double interval = double(1.0 / shares);
    for(int i = 0; i <= shares; ++i)
    {
        for(int j = 0; j <= shares; ++j)
        {
            double u = i * interval;
            double v = j * interval;
            // no interpolation at corners
            if((abs(u) < tol && abs(v) < tol) || (abs(u) < tol && abs(v-1) < tol)
                    || (abs(u-1) < tol && abs(v) < tol) || (abs(u-1) < tol && abs(v-1) < tol))
                continue;
            std::pair<double, double> uv = make_pair(u, v);
            mInterpolatePositions.push_back(uv);
        }
    }
    if(interpolationLevel == 0)
    {
        mInterpolatePositions.push_back(std::pair<double, double>(0, 0));
    }
    // Hide other nodes.
    HideNodesByClass("vtkMRMLModelNode");

    // Refine up spokes
    mUpSpokes = RefinePartOfSpokes(up, stepSize, endCriterion, maxIter);

    // Refine down spokes
    mDownSpokes = RefinePartOfSpokes(down, stepSize, endCriterion, maxIter);

    // Refine crest spokes
    RefineCrestSpokes(crest, stepSize, endCriterion, maxIter);

    // Update header file
    std::string newHeaderFileName;
    UpdateHeader(headerFileName, mOutputPath, &newHeaderFileName);
    //ShowImpliedBoundary(interpolationLevel, newHeaderFileName, "Refined ");
}
void vtkSlicerSkeletalRepresentationRefinerLogic::InterpolateSrep(int interpolationLevel,
                                                                  std::string& srepFileName)
{
    // Hide other nodes.
    HideNodesByClass("vtkMRMLModelNode");
    std::vector<vtkSpoke*> temp;

    // 1. Parse the model into a parameter array that needs to be optimized
    int nRows = 0, nCols = 0;
    std::string up, down, crest;
    double crestShift = 0.0;
    ParseHeader(srepFileName, &nRows, &nCols, &crestShift, &up, &down, &crest);
    if(nRows == 0 || nCols == 0)
    {
        std::cerr << "The s-rep model is empty." << std::endl;
        return;
    }
    InterpolateSrep(interpolationLevel, nRows, nCols, up, crest, temp);
}

void vtkSlicerSkeletalRepresentationRefinerLogic::InterpolateSrep(int interpolationLevel, int nRows, int nCols,
                                                                  std::string& up, std::string& crest,
                                                                  std::vector<vtkSpoke*> &interpolatedSpokes)
{
    std::vector<double> coeffArrayUp, radiiUp, dirsUp, skeletalPointsUp;
    Parse(up, coeffArrayUp, radiiUp, dirsUp, skeletalPointsUp);


    vtkSrep *srep = new vtkSrep(nRows, nCols, radiiUp, dirsUp, skeletalPointsUp);
    if(srep->IsEmpty())
    {
        std::cerr << "The s-rep model is empty." << std::endl;
        delete srep;
        srep = nullptr;
        return;
    }
    // 1.1 interpolate and visualize for verification
    // collect neighboring spokes around corners
    vtkSlicerSkeletalRepresentationInterpolater interpolater;

    int shares = static_cast<int>(pow(2, interpolationLevel));
    double interval = static_cast<double>(1.0/ shares);
    std::vector<double> steps;

    for(int i = 0; i <= shares; ++i)
    {
        steps.push_back(i * interval);
    }

    for(int r = 0; r < nRows-1; ++r)
    {
        for(int c = 0; c < nCols-1; ++c)
        {
            vtkSpoke *cornerSpokes[4];

            double  dXdu11[3], dXdv11[3],
                    dXdu12[3], dXdv12[3],
                    dXdu21[3], dXdv21[3],
                    dXdu22[3], dXdv22[3];

            for(size_t i = 0; i < steps.size(); ++i)
            {
                for(size_t j = 0; j < steps.size(); ++j)
                {
                    cornerSpokes[0] = srep->GetSpoke(r,c);
                    cornerSpokes[1] = srep->GetSpoke(r+1, c);
                    cornerSpokes[2] = srep->GetSpoke(r+1, c+1);
                    cornerSpokes[3] = srep->GetSpoke(r, c+ 1);

                    ComputeDerivative(skeletalPointsUp, r, c, nRows, nCols, dXdu11, dXdv11);
                    ComputeDerivative(skeletalPointsUp, r+1, c, nRows, nCols, dXdu21, dXdv21);
                    ComputeDerivative(skeletalPointsUp, r, c+1, nRows, nCols, dXdu12, dXdv12);
                    ComputeDerivative(skeletalPointsUp, r+1, c+1, nRows, nCols, dXdu22, dXdv22);
                    interpolater.SetCornerDxdu(dXdu11,
                                               dXdu21,
                                               dXdu22,
                                               dXdu12);
                    interpolater.SetCornerDxdv(dXdv11,
                                               dXdv21,
                                               dXdv22,
                                               dXdv12);

                    vtkSpoke* in1 = new vtkSpoke;
                    interpolater.Interpolate(double(steps[i]), double(steps[j]), cornerSpokes, in1);
                    interpolatedSpokes.push_back(in1);

                }
            }
        }
    }

    vtkSmartPointer<vtkPolyData> upSpokes_polyData = vtkSmartPointer<vtkPolyData>::New();
    ConvertSpokes2PolyData(interpolatedSpokes, upSpokes_polyData);
    Visualize(upSpokes_polyData, "Interpolated", 1, 1, 1);

    vtkSmartPointer<vtkPolyData> primarySpokes = vtkSmartPointer<vtkPolyData>::New();
    ConvertSpokes2PolyData(srep->GetAllSpokes(), primarySpokes);
    Visualize(primarySpokes, "Primary", 1, 0, 0);

    std::vector<vtkSpoke*> crestSpokes, topCrest, crestInterpolate;
    ParseCrest(crest, crestSpokes);

    std::vector<vtkSpoke *> tempSpokes;
    InterpolateCrest(crestSpokes, interpolatedSpokes, interpolationLevel, nRows, nCols, crestInterpolate, tempSpokes);

    vtkSmartPointer<vtkPolyData> crestSpokes_poly = vtkSmartPointer<vtkPolyData>::New();
    ConvertSpokes2PolyData(crestInterpolate, crestSpokes_poly);
    Visualize(crestSpokes_poly, "Crest", 0, 0, 1);

    vtkSmartPointer<vtkPolyData> crestSpokes_primary = vtkSmartPointer<vtkPolyData>::New();
    ConvertSpokes2PolyData(crestSpokes, crestSpokes_primary);
    Visualize(crestSpokes_primary, "Crest Primary", 0, 1, 1);
    // delete pointers
    delete srep;
}

void vtkSlicerSkeletalRepresentationRefinerLogic::SetWeights(double wtImageMatch, double wtNormal, double wtSrad)
{
    mWtImageMatch = wtImageMatch;
    mWtNormalMatch = wtNormal;
    mWtSrad = wtSrad;
}

double vtkSlicerSkeletalRepresentationRefinerLogic::operator ()(double *coeff)
{
    double cost = 0.0;
    cost = EvaluateObjectiveFunction(coeff);
    return cost;
}
double vtkSlicerSkeletalRepresentationRefinerLogic::EvaluateObjectiveFunction(double *coeff)
{
    // TODO: SHOULD add progress bar here.
    //std::cout << "Current iteration: " << iterNum++ << std::endl;

    if(mSrep == nullptr)
    {
        std::cerr << "The srep pointer in the refinement is nullptr." << std::endl;
        return -100000.0;
    }

    // this temporary srep is constructed to compute the cost function value
    // The original srep should not be changed by each iteration
    vtkSrep *tempSrep = new vtkSrep();
    tempSrep->DeepCopy(*mSrep);
    tempSrep->Refine(coeff);
    double imageDist = 0.0, normal = 0.0, srad = 0.0;
    int paramDim = static_cast<int>(mCoeffArray.size());
    int spokeNum = paramDim / 4;
    // 1. Compute image match from all spokes and those spokes affected by them
    for(int i = 0; i < spokeNum; ++i)
    {
        int r = i / mNumCols;
        int c = i % mNumCols;
        vtkSpoke *thisSpoke = tempSrep->GetSpoke(r, c);

        // compute distance for this spoke
        imageDist += ComputeDistance(thisSpoke, &normal);

        for(auto it = mInterpolatePositions.begin(); it != mInterpolatePositions.end(); ++it)
        {
            double u = (*it).first;
            double v = (*it).second;

            // For each spoke at the corner of the srep,
            // its neighbors are all spokes in one quad
            if(r == 0 && c == 0)
            {
                // left-top corner
                imageDist += TotalDistOfLeftTopSpoke(tempSrep, u, v, r, c, &normal);
            }
            else if(r == 0 && c == mNumCols - 1)
            {
                // right-top corner
                imageDist += TotalDistOfRightTopSpoke(tempSrep, u, v, r,c, &normal);
            }
            else if(r == mNumRows - 1 && c == 0)
            {
                // left-bot corner
                imageDist += TotalDistOfLeftBotSpoke(tempSrep, u, v, r,c, &normal);
            }
            else if(r == mNumRows - 1 && c == mNumCols - 1)
            {
                // right-bot corner
                imageDist += TotalDistOfRightBotSpoke(tempSrep, u, v, r, c, &normal);
            }
            // For each spoke on the edge of the srep,
            // its neighbors are all spokes in two quads
            else if(r == 0)
            {
                // top edge in middle
                imageDist += TotalDistOfRightTopSpoke(tempSrep, u, v, r, c, &normal);
                imageDist += TotalDistOfLeftTopSpoke(tempSrep, u, v, r, c, &normal);
            }
            else if(r == mNumRows - 1)
            {
                // bot edge in middle
                imageDist += TotalDistOfRightBotSpoke(tempSrep, u, v, r, c, &normal);
                imageDist += TotalDistOfLeftBotSpoke(tempSrep, u, v, r, c, &normal);
            }
            else if(c == 0)
            {
                // left edge in middle
                imageDist += TotalDistOfLeftBotSpoke(tempSrep, u, v, r, c, &normal);
                imageDist += TotalDistOfLeftTopSpoke(tempSrep, u, v, r, c, &normal);
            }
            else if(c == mNumCols - 1)
            {
                // right edge in middle
                imageDist += TotalDistOfRightBotSpoke(tempSrep, u, v, r, c, &normal);
                imageDist += TotalDistOfRightTopSpoke(tempSrep, u, v, r, c, &normal);
            }
            // for each spoke in the middle of the srep,
            // obtain image distance and normal from all interpolated spoke in 4 quads around it
            else
            {
                imageDist += TotalDistOfRightBotSpoke(tempSrep, u, v, r, c, &normal);
                imageDist += TotalDistOfRightTopSpoke(tempSrep, u, v, r, c, &normal);

                imageDist += TotalDistOfLeftBotSpoke(tempSrep, u, v, r, c, &normal);
                imageDist += TotalDistOfLeftTopSpoke(tempSrep, u, v, r, c, &normal);
            }
        }

    }

    // 2. compute srad penalty
    srad = ComputeRSradPenalty(tempSrep);

    if(mFirstCost)
    {
        // this log helps to adjust the weights of three terms
        std::cout << "ImageMatch:" << imageDist << ", normal:" << normal << ", srad:" << srad << std::endl;
        mFirstCost = false;
    }

    delete tempSrep;
    return mWtImageMatch * imageDist + mWtNormalMatch * normal + mWtSrad * srad;
}

void vtkSlicerSkeletalRepresentationRefinerLogic::AntiAliasSignedDistanceMap(const std::string &meshFileName)
{
    // 1. convert poly data to image data
    vtkPolyData2ImageData polyDataConverter;
    vtkSmartPointer<vtkImageData> img = vtkSmartPointer<vtkImageData>::New();

    // this conversion already put the image into the unit-cube
    polyDataConverter.Convert(meshFileName, img);

    vtkSmartPointer<vtkImageData> antiAliasedImage = vtkSmartPointer<vtkImageData>::New();

    vtkApproximateSignedDistanceMap ssdGenerator;
    ssdGenerator.Convert(img, mAntiAliasedImage);

    // 4. compute normals of the image everywhere
    vtkGradientDistanceFilter gradDistFilter;
    gradDistFilter.Filter(mAntiAliasedImage, mGradDistImage);
}

void vtkSlicerSkeletalRepresentationRefinerLogic::TransformSrep(const std::string &headerFile)
{

    int nRows = 0, nCols = 0;
    std::string up, down, crest;
    double crestShift = 0.0;
    ParseHeader(headerFile, &nRows, &nCols, &crestShift, &up, &down, &crest);

    if(nRows == 0 || nCols == 0)
    {
        std::cerr << "The s-rep model is empty." << std::endl;
        return;
    }

    std::vector<double> radiiUp, dirsUp, skeletalPointsUp, coeffUp;
    Parse(up, coeffUp, radiiUp, dirsUp, skeletalPointsUp);

    vtkSrep *srep = new vtkSrep(nRows, nCols, radiiUp, dirsUp, skeletalPointsUp);
    if(srep->IsEmpty())
    {
        std::cerr << "The s-rep model is empty." << std::endl;
        delete srep;
        srep = nullptr;
        return;
    }

    std::vector<double> radiiDown, dirsDown, skeletalPointsDown, coeffDown;
    Parse(down, coeffDown, radiiDown, dirsDown, skeletalPointsDown);
    srep->AddSpokes(radiiDown, dirsDown, skeletalPointsDown);

    std::vector<double> radiiCrest, dirsCrest, skeletalPointsCrest, coeffCrest;
    Parse(crest, coeffCrest, radiiCrest, dirsCrest, skeletalPointsCrest);
    srep->AddSpokes(radiiCrest, dirsCrest, skeletalPointsCrest);

    TransformSrep2ImageCS(srep, mTransformationMat);

//    vtkSmartPointer<vtkPolyData> primarySpokes = vtkSmartPointer<vtkPolyData>::New();
//    ConvertSpokes2PolyData(srep->GetAllSpokes(), primarySpokes);
//    Visualize(primarySpokes, "Primary", 1, 0, 0);

//    vtkSmartPointer<vtkPolyData> transUpPrimary = vtkSmartPointer<vtkPolyData>::New();
//    TransSpokes2PolyData(srep->GetAllSpokes(), transUpPrimary);
//    Visualize(transUpPrimary, "Transformed primary", 1, 0, 0);
    delete srep;
}

void vtkSlicerSkeletalRepresentationRefinerLogic::ShowImpliedBoundary(int interpolationLevel,
                                                                      const string &srepFileName,
                                                                      const std::string& modelPrefix)
{
    // Hide other nodes.
    HideNodesByClass("vtkMRMLModelNode");

    vtkSmartPointer<vtkPolyDataReader> reader = vtkSmartPointer<vtkPolyDataReader>::New();
    reader->SetFileName(mTargetMeshFilePath.c_str());
    reader->Update();
    vtkSmartPointer<vtkPolyData> inputMesh = reader->GetOutput();
    // 1. Parse the model into a parameter array that needs to be optimized
    int nRows = 0, nCols = 0;
    std::string up, down, crest;
    double crestShift = 0.0;
    ParseHeader(srepFileName, &nRows, &nCols, &crestShift, &up, &down, &crest);

    std::vector<vtkSpoke*> interpolatedSpokes, upSpokes, downSpokes, crestSpokes;
    vtkSmartPointer<vtkPolyData> wireFrame = vtkSmartPointer<vtkPolyData>::New();
    vtkSmartPointer<vtkPoints> pts, foldCurvePts;
    vtkSmartPointer<vtkCellArray> quads, foldCurveCell;
    pts = vtkSmartPointer<vtkPoints>::New();
    foldCurvePts = vtkSmartPointer<vtkPoints>::New();
    quads = vtkSmartPointer<vtkCellArray>::New();
    foldCurveCell = vtkSmartPointer<vtkCellArray>::New();

    vtkSmartPointer<vtkPolyData> foldCurve = vtkSmartPointer<vtkPolyData>::New();

    // connect implied boundary for up spokes
    ConnectImpliedBoundaryPts(interpolationLevel, nRows, nCols, up,
                              wireFrame, foldCurvePts,
                              foldCurveCell, interpolatedSpokes, upSpokes);

    // connect implied boundary for down spokes
    ConnectImpliedBoundaryPts(interpolationLevel, nRows, nCols, down,
                              wireFrame, foldCurvePts, foldCurveCell,
                              interpolatedSpokes, downSpokes);

//    wireFrame->SetPoints(pts);
//    wireFrame->SetPolys(quads);
//    Visualize(wireFrame, modelPrefix + "Wire frame", 0, 1, 1);
//    wireFrame->GetPointData()->SetNormals()
    //ConvertPointCloud2Mesh(inputMesh);


    vtkSmartPointer<vtkAppendPolyData> appendFilter =
      vtkSmartPointer<vtkAppendPolyData>::New();
    appendFilter->AddInputData(wireFrame);

    ConnectImpliedCrest(interpolationLevel, nRows, nCols, crest, upSpokes, downSpokes, appendFilter);
    foldCurve->SetPoints(foldCurvePts);
    foldCurve->SetPolys(foldCurveCell);
    Visualize(foldCurve, modelPrefix + "Fold curve", 0, 1, 0, false);

    vtkSmartPointer<vtkPolyData> polySpokes = vtkSmartPointer<vtkPolyData>::New();
    ConvertSpokes2PolyData(interpolatedSpokes, polySpokes);
    Visualize(polySpokes, modelPrefix + "Primary spokes", 1, 0,0, false);

    // show difference between implied boundary and the target object
    vtkSmartPointer<vtkPolyData> impliedBoundary = vtkSmartPointer<vtkPolyData>::New();
    appendFilter->Update();
    vtkSmartPointer<vtkCleanPolyData> cleanFilter =
        vtkSmartPointer<vtkCleanPolyData>::New();
    cleanFilter->SetInputConnection(appendFilter->GetOutputPort());
    cleanFilter->Update();
    impliedBoundary = cleanFilter->GetOutput();
    vtkSmartPointer<vtkPolyData> heatMap = vtkSmartPointer<vtkPolyData>::New();
    vtkSmartPointer<vtkUnsignedCharArray> colors =
      vtkSmartPointer<vtkUnsignedCharArray>::New();
    colors->SetNumberOfComponents(3);
    colors->SetName("Colors");
    double minDist=1000, maxDist = -1;

    vtkSmartPointer<vtkPoints> surfacePts = vtkSmartPointer<vtkPoints>::New();
    vtkSmartPointer<vtkDistancePolyDataFilter> distanceFilter =
      vtkSmartPointer<vtkDistancePolyDataFilter>::New();

    distanceFilter->SetInputData( 0, inputMesh);
    distanceFilter->SetInputData( 1, impliedBoundary);
    distanceFilter->Update();
    vtkSmartPointer<vtkDataArray> distArray = distanceFilter->GetOutput()->GetPointData()->GetScalars();
    minDist = distArray->GetRange()[0];
    maxDist = distArray->GetRange()[1];
    double sumDistance = 0;
    for(int i = 0; i < distArray->GetNumberOfTuples(); ++i) {
        double d = distArray->GetTuple1(i);
        sumDistance += d * d;
    }
    std::cout << "minimum distance: " << minDist <<
                 " and maximum distance: " << maxDist << " . The ssd is:" << sumDistance << std::endl;
    vtkMRMLScene *scene = this->GetMRMLScene();
    if(!scene)
    {
        vtkErrorMacro(" Invalid scene");
        return;
    }

    // model node
    vtkSmartPointer<vtkMRMLModelNode> modelNode;
    modelNode = vtkSmartPointer<vtkMRMLModelNode>::New();
    modelNode->SetScene(scene);
    modelNode->SetName("heat map");
    modelNode->SetAndObservePolyData(distanceFilter->GetOutput());

    // display node
    vtkSmartPointer<vtkMRMLModelDisplayNode> displayModelNode;
    vtkSmartPointer<vtkColorTransferFunction> colorTransferFunction =
        vtkSmartPointer<vtkColorTransferFunction>::New();
    colorTransferFunction->AddRGBPoint(minDist, 0, 0, 1);
    colorTransferFunction->AddRGBPoint(maxDist, 1, 0, 0);
    vtkSmartPointer<vtkMRMLProceduralColorNode> colorNode = vtkSmartPointer<vtkMRMLProceduralColorNode>::New();
    displayModelNode = vtkSmartPointer<vtkMRMLModelDisplayNode>::New();
    if(displayModelNode == nullptr)
    {
        vtkErrorMacro("displayModelNode is NULL");
        return;
    }
    colorNode->SetAndObserveColorTransferFunction(colorTransferFunction);
    displayModelNode->SetAndObserveColorNodeID(colorNode->GetID());
    displayModelNode->SetScalarRangeFlag(2);
    displayModelNode->SetScalarRange(minDist, maxDist);
    displayModelNode->SetScene(scene);
//    displayModelNode->SetLineWidth(2.0);
//    displayModelNode->SetBackfaceCulling(0);
//    displayModelNode->SetRepresentation(1);
    modelNode->AddAndObserveDisplayNodeID(displayModelNode->GetID());
    scene->AddNode(displayModelNode);
    scene->AddNode(modelNode);
}

void vtkSlicerSkeletalRepresentationRefinerLogic::CLIRefine(const std::string &srepFileName,
                                                            const std::string &imgFileName,
                                                            const std::string &outputPath,
                                                            double stepSize, double endCriterion,
                                                            int maxIter, double wtImg, double wtNormal, double wtSrad,
                                                            int interpolationLevel)
{
    SetSrepFileName(srepFileName);
    SetImageFileName(imgFileName);
    SetOutputPath(outputPath);
    SetWeights(wtImg, wtNormal, wtSrad);
    Refine(stepSize, endCriterion, maxIter, interpolationLevel);
}

void vtkSlicerSkeletalRepresentationRefinerLogic::ComputeDerivative(std::vector<double> skeletalPoints, int intr, int intc, int nRows, int intCols, double *dXdu, double *dXdv)
{
    // 0-based index of elements if arranged in array
    size_t nCols = static_cast<size_t>(intCols);
    size_t r = static_cast<size_t>(intr);
    size_t c= static_cast<size_t>(intc);
    size_t id = static_cast<size_t>(r * nCols + c);
    double head[3], tail[3];
    double factor = 0.5;
    if(r == 0)
    {
        // first row
        // forward difference, next row/col - current row/col
        head[0] = skeletalPoints[(id+nCols)*3];
        head[1] = skeletalPoints[(id+nCols)*3+1];
        head[2] = skeletalPoints[(id+nCols)*3+2];

        tail[0] = skeletalPoints[(id)*3];
        tail[1] = skeletalPoints[(id)*3+1];
        tail[2] = skeletalPoints[(id)*3+2];
        factor = 1.0;
    }
    else if(r == static_cast<size_t>(nRows - 1))
    {
        // last row
        // backward difference
        tail[0] = skeletalPoints[(id-nCols)*3];
        tail[1] = skeletalPoints[(id-nCols)*3+1];
        tail[2] = skeletalPoints[(id-nCols)*3+2];

        head[0] = skeletalPoints[(id)*3];
        head[1] = skeletalPoints[(id)*3+1];
        head[2] = skeletalPoints[(id)*3+2];
        factor = 1.0;
    }
    else
    {
        // otherwise, center difference
        head[0] = skeletalPoints[(id+nCols)*3];
        head[1] = skeletalPoints[(id+nCols)*3+1];
        head[2] = skeletalPoints[(id+nCols)*3+2];

        tail[0] = skeletalPoints[(id-nCols)*3];
        tail[1] = skeletalPoints[(id-nCols)*3+1];
        tail[2] = skeletalPoints[(id-nCols)*3+2];
        factor = 0.5;
    }
    ComputeDiff(head, tail, factor, dXdu);

    if(c == 0)
    {
        // first col
        head[0] = skeletalPoints[(id+1)*3];
        head[1] = skeletalPoints[(id+1)*3+1];
        head[2] = skeletalPoints[(id+1)*3+2];

        tail[0] = skeletalPoints[(id)*3];
        tail[1] = skeletalPoints[(id)*3+1];
        tail[2] = skeletalPoints[(id)*3+2];
        factor = 1.0;
    }
    else if(c == nCols - 1)
    {
        // last col
        // backward difference
        tail[0] = skeletalPoints[(id-1)*3];
        tail[1] = skeletalPoints[(id-1)*3+1];
        tail[2] = skeletalPoints[(id-1)*3+2];

        head[0] = skeletalPoints[(id)*3];
        head[1] = skeletalPoints[(id)*3+1];
        head[2] = skeletalPoints[(id)*3+2];
        factor = 1.0;
    }
    else
    {
        // otherwise, center difference
        head[0] = skeletalPoints[(id+1)*3];
        head[1] = skeletalPoints[(id+1)*3+1];
        head[2] = skeletalPoints[(id+1)*3+2];

        tail[0] = skeletalPoints[(id-1)*3];
        tail[1] = skeletalPoints[(id-1)*3+1];
        tail[2] = skeletalPoints[(id-1)*3+2];
        factor = 0.5;
    }
    ComputeDiff(head, tail, factor, dXdv);

}

void vtkSlicerSkeletalRepresentationRefinerLogic::ConvertSpokes2PolyData(std::vector<vtkSpoke *>input, vtkPolyData *output)
{
    vtkSmartPointer<vtkPoints> pts = vtkSmartPointer<vtkPoints>::New();
    vtkSmartPointer<vtkCellArray> arrows = vtkSmartPointer<vtkCellArray>::New();

    size_t numSpokes = input.size();
    for(size_t i = 0; i < numSpokes; ++i)
    {
        vtkSpoke* currSpoke = input[i];
        double basePt[3], bdryPt[3], dir[3];
        currSpoke->GetSkeletalPoint(basePt);
        currSpoke->GetBoundaryPoint(bdryPt);
        currSpoke->GetDirection(dir);
        vtkIdType id0 = (pts->InsertNextPoint(basePt[0], basePt[1], basePt[2]));
        vtkIdType id1 = pts->InsertNextPoint(bdryPt[0], bdryPt[1], bdryPt[2]);

        vtkSmartPointer<vtkLine> currLine = vtkSmartPointer<vtkLine>::New();
        currLine->GetPointIds()->SetId(0, id0);
        currLine->GetPointIds()->SetId(1, id1);
        arrows->InsertNextCell(currLine);
    }
    output->SetPoints(pts);
    output->SetLines(arrows);
    output->Modified();
}

void vtkSlicerSkeletalRepresentationRefinerLogic::SaveSpokes2Vtp(std::vector<vtkSpoke *> input, const string &path)
{
    vtkSmartPointer<vtkPoints> pts = vtkSmartPointer<vtkPoints>::New();
    vtkSmartPointer<vtkDoubleArray> spokeDirection = vtkSmartPointer<vtkDoubleArray>::New();
    vtkSmartPointer<vtkDoubleArray> spokeLengths = vtkSmartPointer<vtkDoubleArray>::New();

    size_t numSpokes = input.size();
    spokeLengths->SetNumberOfComponents(1);
    spokeLengths->SetName("spokeLength");

    spokeDirection->SetNumberOfComponents(3);
    spokeDirection->SetName("spokeDirection");

    for(size_t i = 0; i < numSpokes; ++i)
    {
        vtkSpoke* currSpoke = input[i];
        double basePt[3], dir[3], radius;
        currSpoke->GetSkeletalPoint(basePt);
        radius = currSpoke->GetRadius();
        currSpoke->GetDirection(dir);
        pts->InsertNextPoint(basePt[0], basePt[1], basePt[2]);
        spokeDirection->InsertNextTuple(dir);
        spokeLengths->InsertNextTuple1(radius);
    }
    vtkSmartPointer<vtkPolyData> output = vtkSmartPointer<vtkPolyData>::New();
    output->SetPoints(pts);

    output->GetPointData()->AddArray(spokeDirection);
    output->GetPointData()->SetActiveVectors("spokeDirection");
    output->GetPointData()->AddArray(spokeLengths);
    output->GetPointData()->SetActiveScalars("spokeLength");

    vtkSmartPointer<vtkXMLPolyDataWriter> writer = vtkSmartPointer<vtkXMLPolyDataWriter>::New();
    writer->SetFileName(path.c_str());
    writer->SetInputData(output);
    writer->Update();
}

void vtkSlicerSkeletalRepresentationRefinerLogic::TransSpokes2PolyData(std::vector<vtkSpoke *>input, vtkPolyData *output)
{
    vtkSmartPointer<vtkPoints> pts = vtkSmartPointer<vtkPoints>::New();
    vtkSmartPointer<vtkCellArray> arrows = vtkSmartPointer<vtkCellArray>::New();

    for(size_t i = 0; i < input.size(); ++i)
    {
        vtkSpoke* currSpoke = input[i];
        double basePt[3], bdryPt[3];
        currSpoke->GetSkeletalPoint(basePt);
        currSpoke->GetBoundaryPoint(bdryPt);

        basePt[0] = basePt[0] * mTransformationMat[0][0] + mTransformationMat[3][0];
        basePt[1] = basePt[1] * mTransformationMat[1][1] + mTransformationMat[3][1];
        basePt[2] = basePt[2] * mTransformationMat[2][2] + mTransformationMat[3][2];

        bdryPt[0] = bdryPt[0] * mTransformationMat[0][0] + mTransformationMat[3][0];
        bdryPt[1] = bdryPt[1] * mTransformationMat[1][1] + mTransformationMat[3][1];
        bdryPt[2] = bdryPt[2] * mTransformationMat[2][2] + mTransformationMat[3][2];

        vtkIdType id0 = pts->InsertNextPoint(basePt[0], basePt[1], basePt[2]);
        vtkIdType id1 = pts->InsertNextPoint(bdryPt[0], bdryPt[1], bdryPt[2]);

        vtkSmartPointer<vtkLine> currLine = vtkSmartPointer<vtkLine>::New();
        currLine->GetPointIds()->SetId(0, id0);
        currLine->GetPointIds()->SetId(1, id1);
        arrows->InsertNextCell(currLine);
    }
    output->SetPoints(pts);
    output->SetLines(arrows);
}

void vtkSlicerSkeletalRepresentationRefinerLogic::VisualizePoints(vtkPoints *input)
{
    vtkMRMLScene *scene = this->GetMRMLScene();
    if(!scene)
    {
        vtkErrorMacro(" Invalid scene");
        return;
    }

    vtkSmartPointer<vtkMRMLMarkupsFiducialNode> fidNode = vtkSmartPointer<vtkMRMLMarkupsFiducialNode>::New();

    vtkSmartPointer<vtkMRMLMarkupsDisplayNode> fidDisplayNode = vtkSmartPointer<vtkMRMLMarkupsDisplayNode>::New();
    scene->AddNode(fidDisplayNode);
    fidDisplayNode->SetGlyphScale(0.01);
    fidDisplayNode->SetSelectedColor(1.0, 1.0, 0.0);
    fidDisplayNode->SetTextScale(0.0);
    fidDisplayNode->SetName("surface points");
    scene->AddNode(fidNode);
    fidNode->SetAndObserveDisplayNodeID(fidDisplayNode->GetID());


    fidNode->SetLocked(true);
    for (int i = 0; i < input->GetNumberOfPoints(); ++i) {
        double pt[3];
        input->GetPoint(i, pt);
        fidNode->AddFiducial(pt[0], pt[1], pt[2]);
    }

}

//---------------------------------------------------------------------------
void vtkSlicerSkeletalRepresentationRefinerLogic::SetMRMLSceneInternal(vtkMRMLScene * newScene)
{
  vtkNew<vtkIntArray> events;
  events->InsertNextValue(vtkMRMLScene::NodeAddedEvent);
  events->InsertNextValue(vtkMRMLScene::NodeRemovedEvent);
  events->InsertNextValue(vtkMRMLScene::EndBatchProcessEvent);
  this->SetAndObserveMRMLSceneEventsInternal(newScene, events.GetPointer());
}

//-----------------------------------------------------------------------------
void vtkSlicerSkeletalRepresentationRefinerLogic::RegisterNodes()
{
  assert(this->GetMRMLScene() != nullptr);
}

//---------------------------------------------------------------------------
void vtkSlicerSkeletalRepresentationRefinerLogic::UpdateFromMRMLScene()
{
  assert(this->GetMRMLScene() != nullptr);
}

//---------------------------------------------------------------------------
void vtkSlicerSkeletalRepresentationRefinerLogic
::OnMRMLSceneNodeAdded(vtkMRMLNode* vtkNotUsed(node))
{
}

//---------------------------------------------------------------------------
void vtkSlicerSkeletalRepresentationRefinerLogic
::OnMRMLSceneNodeRemoved(vtkMRMLNode* vtkNotUsed(node))
{
}

void vtkSlicerSkeletalRepresentationRefinerLogic::Parse(const std::string &modelFileName, std::vector<double> &coeffArray,
                                                        std::vector<double> &radii, std::vector<double> &dirs, std::vector<double> &skeletalPoints)
{

    vtkSmartPointer<vtkXMLPolyDataReader> reader = vtkSmartPointer<vtkXMLPolyDataReader>::New();
    reader->SetFileName(modelFileName.c_str());
    reader->Update();

    vtkSmartPointer<vtkPolyData> spokesPolyData = reader->GetOutput();
    vtkSmartPointer<vtkPointData> spokesPointData = spokesPolyData->GetPointData();
    int numOfArrays = spokesPointData->GetNumberOfArrays();
    vtkIdType numOfSpokes = spokesPolyData->GetNumberOfPoints();

    if(numOfSpokes == 0 || numOfArrays == 0)
    {
        return;
    }

    // including Ux, Uy, Uz, r
    vtkSmartPointer<vtkDoubleArray> spokeRadii = vtkDoubleArray::SafeDownCast(spokesPointData->GetArray("spokeLength"));
    vtkSmartPointer<vtkDoubleArray> spokeDirs = vtkDoubleArray::SafeDownCast(spokesPointData->GetArray("spokeDirection"));

    for(int i = 0; i < numOfSpokes; ++i)
    {
        int idxDir = i * 3; // Ux, Uy, Uz

        // coefficients (dirs + radii) for newuoa
        // the coefficient for radii is the exponential value, initially 0
        coeffArray.push_back(spokeDirs->GetValue(idxDir+0));
        coeffArray.push_back(spokeDirs->GetValue(idxDir+1));
        coeffArray.push_back(spokeDirs->GetValue(idxDir+2));
        //coeffArray.push_back(spokeRadii->GetValue(i));
        coeffArray.push_back(0);

        // data for spokes
        radii.push_back(spokeRadii->GetValue(i));

        dirs.push_back(spokeDirs->GetValue(idxDir+0));
        dirs.push_back(spokeDirs->GetValue(idxDir+1));
        dirs.push_back(spokeDirs->GetValue(idxDir+2));

        double tempSkeletalPoint[3];
        spokesPolyData->GetPoint(i, tempSkeletalPoint);
        skeletalPoints.push_back(tempSkeletalPoint[0]);
        skeletalPoints.push_back(tempSkeletalPoint[1]);
        skeletalPoints.push_back(tempSkeletalPoint[2]);
    }
}

void vtkSlicerSkeletalRepresentationRefinerLogic::ParseHeader(const std::string &headerFileName, int *nRows, int *nCols,
                                                               double *shift, std::string* upFileName,
                                                              std::string* downFileName, std::string* crestFileName)
{
    vtkSmartPointer<vtkXMLDataParser> parser = vtkSmartPointer<vtkXMLDataParser>::New();

    parser->SetFileName(headerFileName.c_str());
    parser->SetIgnoreCharacterData(0);

    if( parser->Parse() == 1)
    {
        vtkXMLDataElement *root = parser->GetRootElement();
        int numElements = root->GetNumberOfNestedElements();;
        for(int i = 0; i < numElements; ++i)
        {
            int r, c;
            double crestShift;
            char *pEnd;
            vtkXMLDataElement *e = root->GetNestedElement(i);
            std::string estimatePath;
            estimatePath = vtksys::SystemTools::GetFilenamePath(headerFileName) + "/";
            std::vector<std::string> components;
            components.push_back(estimatePath);

            char* eName = e->GetName();
            if(strcmp(eName, "nRows") == 0)
            {
                r = static_cast<int>(strtol(e->GetCharacterData(), &pEnd, 10));
                *nRows = r;
            }
            else if(strcmp(eName, "nCols") == 0)
            {
                c = static_cast<int>(strtol(e->GetCharacterData(), &pEnd, 10));
                *nCols = c;
            }
            else if(strcmp(eName, "upSpoke") == 0)
            {
                *upFileName = e->GetCharacterData();
                // some file paths are relative path, others are absolute path
                if(!vtksys::SystemTools::FileIsFullPath(*upFileName))
                {
                    components.push_back(*upFileName);
                    *upFileName = vtksys::SystemTools::JoinPath(components);

                }
                // change to relative path
                *upFileName = estimatePath+ "up.vtp";
            }
            else if(strcmp(eName, "downSpoke")==0)
            {
                *downFileName = e->GetCharacterData();
                if(!vtksys::SystemTools::FileIsFullPath(*downFileName))
                {
                    components.push_back(*downFileName);
                    *downFileName = vtksys::SystemTools::JoinPath(components);
                }
                // change to relative path
                *downFileName = estimatePath + "down.vtp";
            }
            else if(strcmp(eName, "crestSpoke") == 0)
            {
                *crestFileName = e->GetCharacterData();
                if(!vtksys::SystemTools::FileIsFullPath(*crestFileName))
                {
                    components.push_back(*crestFileName);
                    *crestFileName = vtksys::SystemTools::JoinPath(components);
                }
                *crestFileName =estimatePath + "crest.vtp";
            }
            else if(strcmp(eName, "crestShift") == 0)
            {
                crestShift = atof(e->GetCharacterData());
                *shift = crestShift;
            }
        }
    }

}

void vtkSlicerSkeletalRepresentationRefinerLogic::UpdateHeader(const string &headerFileName,
                                                               const string &outputFilePath,
                                                               std::string *newHeaderFileName)
{
    vtkSmartPointer<vtkXMLDataParser> parser = vtkSmartPointer<vtkXMLDataParser>::New();

    parser->SetFileName(headerFileName.c_str());
    parser->SetIgnoreCharacterData(0);
    if( parser->Parse() == 1)
    {
        vtkXMLDataElement *root = parser->GetRootElement();
        int numElements = root->GetNumberOfNestedElements();;
        std::string newUpFileName, newDownFileName, newCrestFileName;

        std::string estimatePath;
        estimatePath = vtksys::SystemTools::GetFilenamePath(headerFileName) + "/";

        int nRows = 0, nCols = 0;
        for(int i = 0; i < numElements; ++i)
        {
            int r, c;
            char *pEnd;
            vtkXMLDataElement *e = root->GetNestedElement(i);
            std::string estimatePath;
            estimatePath = vtksys::SystemTools::GetFilenamePath(headerFileName) + "/";
            std::vector<std::string> components;
            components.push_back(estimatePath);
            char* eName = e->GetName();
            if(strcmp(eName, "nRows") == 0)
            {
                r = static_cast<int>(strtol(e->GetCharacterData(), &pEnd, 10));
                nRows = r;
            }
            else if(strcmp(eName, "nCols") == 0)
            {
                c = static_cast<int>(strtol(e->GetCharacterData(), &pEnd, 10));
                nCols = c;
            }
            else if(strcmp(eName, "upSpoke") == 0)
            {
                std::string oldFile(e->GetCharacterData());
                // some file paths are relative path, others are absolute path
                oldFile = vtksys::SystemTools::GetFilenameName(oldFile);
                newUpFileName = outputFilePath + newFilePrefix + oldFile;
            }
            else if(strcmp(eName, "downSpoke")==0)
            {
                std::string oldFile(e->GetCharacterData());
                // some file paths are relative path, others are absolute path
                oldFile = vtksys::SystemTools::GetFilenameName(oldFile);
                newDownFileName = outputFilePath + newFilePrefix + oldFile;
            }
            else if(strcmp(eName, "crestSpoke")==0)
            {
                std::string oldFile(e->GetCharacterData());
                // some file paths are relative path, others are absolute path
                oldFile = vtksys::SystemTools::GetFilenameName(oldFile);
                newCrestFileName = outputFilePath + newFilePrefix + oldFile;
            }

        }
        std::stringstream output;

        output<<"<s-rep>"<<std::endl;
        output<<"  <nRows>"<<nRows<<"</nRows>"<<std::endl;
        output<<"  <nCols>"<<nCols<<"</nCols>"<<std::endl;
        output<<"  <meshType>Quad</meshType>"<< std::endl;
        output<<"  <color>"<<std::endl;
        output<<"    <red>0</red>"<<std::endl;
        output<<"    <green>0.5</green>"<<std::endl;
        output<<"    <blue>0</blue>"<<std::endl;
        output<<"  </color>"<<std::endl;
        output<<"  <isMean>False</isMean>"<<std::endl;
        output<<"  <meanStatPath/>"<<std::endl;
        output<<"  <upSpoke>"<< newUpFileName<<"</upSpoke>"<<std::endl;
        output<<"  <downSpoke>"<< newDownFileName << "</downSpoke>"<<std::endl;
        output<<"  <crestSpoke>"<< newCrestFileName << "</crestSpoke>"<<std::endl;
        output<<"</s-rep>"<<std::endl;

        std::string oldHeader = vtksys::SystemTools::GetFilenameName(headerFileName);
        oldHeader = outputFilePath + newFilePrefix + oldHeader;
        std::string header_file(oldHeader);
        std::ofstream out_file;
        out_file.open(header_file);
        out_file << output.rdbuf();
        out_file.close();
        *newHeaderFileName = header_file;
    }
}

void vtkSlicerSkeletalRepresentationRefinerLogic::ComputeDiff(double *head, double *tail, double factor, double *output)
{
    output[0] = factor * (head[0] - tail[0]);
    output[1] = factor * (head[1] - tail[1]);
    output[2] = factor * (head[2] - tail[2]);
}

double vtkSlicerSkeletalRepresentationRefinerLogic::ComputeDistance(vtkSpoke *theSpoke, double *normalMatch)
{
    // 1. Transform the boundary point to image cs. by applying [x, y, z, 1] * mTransformationMat
    double pt[3];
    theSpoke->GetBoundaryPoint(pt);

    pt[0] = pt[0] * mTransformationMat[0][0] + mTransformationMat[3][0];
    pt[1] = pt[1] * mTransformationMat[1][1] + mTransformationMat[3][1];
    pt[2] = pt[2] * mTransformationMat[2][2] + mTransformationMat[3][2];

    pt[0] /= voxelSpacing;
    pt[1] /= voxelSpacing;
    pt[2] /= voxelSpacing;

    int x = static_cast<int>(pt[0]+0.5);
    int y = static_cast<int>(pt[1]+0.5);
    int z = static_cast<int>(pt[2]+0.5);

    int maxX = static_cast<int>(1 / voxelSpacing - 1);
    int maxY = static_cast<int>(1 / voxelSpacing - 1);
    int maxZ = static_cast<int>(1 / voxelSpacing - 1);

    if(x > maxX) x = maxX;
    if(y > maxY) y = maxY;
    if(z > maxZ) z = maxZ;

    if(x < 0) x = 0;
    if(y < 0) y = 0;
    if(z < 0) z = 0;

    if(mAntiAliasedImage == nullptr)
    {
        std::cerr << "The image in this RefinerLogic instance is empty." << std::endl;
        return -10000.0;
    }
    RealImage::IndexType pixelIndex = {{x,y,z}};
    float dist = mAntiAliasedImage->GetPixel(pixelIndex);

    if(mGradDistImage == nullptr)
    {
        return -10000.0;
    }

    VectorImage::IndexType indexGrad;
    indexGrad[0] = x;
    indexGrad[1] = y;
    indexGrad[2] = z;

    VectorImage::PixelType grad = mGradDistImage->GetPixel(indexGrad);
    double normalVector[3];
    normalVector[0] = static_cast<double>(grad[0]);
    normalVector[1] = static_cast<double>(grad[1]);
    normalVector[2] = static_cast<double>(grad[2]);
    // normalize the normal vector
    vtkMath::Normalize(normalVector);

    double spokeDir[3];
    theSpoke->GetDirection(spokeDir);
    double dotProduct = vtkMath::Dot(normalVector, spokeDir);
    double distSqr = static_cast<double>(dist * dist);

    // The normal match (between [0,1]) is scaled by the distance so that the overall term is comparable
    *normalMatch = *normalMatch + distSqr * (1 - dotProduct);
    // return square of distance
    return distSqr;
}

void vtkSlicerSkeletalRepresentationRefinerLogic::Visualize(vtkPolyData *model, const std::string &modelName,
                                                            double r, double g, double b, bool isVisible)
{
    vtkMRMLScene *scene = this->GetMRMLScene();
    if(!scene)
    {
        vtkErrorMacro(" Invalid scene");
        return;
    }

    // model node
    vtkSmartPointer<vtkMRMLModelNode> modelNode;
    modelNode = vtkSmartPointer<vtkMRMLModelNode>::New();
    modelNode->SetScene(scene);
    modelNode->SetName(modelName.c_str());
    modelNode->SetAndObservePolyData(model);

    // display node
    vtkSmartPointer<vtkMRMLModelDisplayNode> displayModelNode;

    displayModelNode = vtkSmartPointer<vtkMRMLModelDisplayNode>::New();
    if(displayModelNode == nullptr)
    {
        vtkErrorMacro("displayModelNode is NULL");
        return;
    }
    displayModelNode->SetColor(r,g,b);
    displayModelNode->SetScene(scene);
    displayModelNode->SetLineWidth(2.0);
    displayModelNode->SetBackfaceCulling(0);
    displayModelNode->SetRepresentation(1);

    if(isVisible)
    {
        // make the 1st mesh after flow visible
        displayModelNode->SetVisibility(1);
    }
    else
    {
        displayModelNode->SetVisibility(0);
    }

    scene->AddNode(displayModelNode);
    modelNode->AddAndObserveDisplayNodeID(displayModelNode->GetID());

    scene->AddNode(modelNode);

}

void vtkSlicerSkeletalRepresentationRefinerLogic::HideNodesByClass(const string &className)
{
    vtkSmartPointer<vtkCollection> modelNodes = this->GetMRMLScene()->GetNodesByClass(className.c_str());
    modelNodes->InitTraversal();
    for(int i = 0; i < modelNodes->GetNumberOfItems(); i++)
    {
        vtkSmartPointer<vtkMRMLModelNode> thisModelNode = vtkMRMLModelNode::SafeDownCast(modelNodes->GetNextItemAsObject());
        vtkSmartPointer<vtkMRMLModelDisplayNode> displayNode;
        displayNode = thisModelNode->GetModelDisplayNode();
        if(displayNode == nullptr)
        {
            continue;
        }

        displayNode->SetVisibility(0);

    }

}

void vtkSlicerSkeletalRepresentationRefinerLogic::TransformSrep2ImageCS(vtkSrep *input, double mat4x4[][4])
{
    if(input->IsEmpty())
    {
        mat4x4 = nullptr;
        return;
    }
    // 1. Find the bounding box of boundary
    std::vector<vtkSpoke *> spokes = input->GetAllSpokes();
    vtkSmartPointer<vtkPoints> boundaryPts =
            vtkSmartPointer<vtkPoints>::New();
    for(size_t i = 0; i < spokes.size(); ++i)
    {
        double pt[3];
        spokes[i]->GetBoundaryPoint(pt);
        boundaryPts->InsertNextPoint(pt);
    }

    double bounds[6];
    boundaryPts->GetBounds(bounds);
    double xrange = bounds[1] - bounds[0];
    double yrange = bounds[3] - bounds[2];
    double zrange = bounds[5] - bounds[4];

    // the new bounding box keep the ratios between x, y, z
    double xrangeTrans, yrangeTrans, zrangeTrans;
    if(xrange >= yrange && xrange >= zrange)
    {
        xrangeTrans = 1.0;
        yrangeTrans = yrange / xrange;
        zrangeTrans = zrange / xrange;

    }
    else if (yrange >= xrange && yrange >= zrange)
    {
        xrangeTrans = xrange / yrange;
        yrangeTrans = 1.0;
        zrangeTrans = zrange / yrange;
    }
    else if (zrange >= xrange && zrange >= yrange)
    {
        xrangeTrans = xrange / zrange;
        yrangeTrans = yrange / zrange;
        zrangeTrans = 1.0;
    }
    else {
        xrangeTrans = 1.0;
        yrangeTrans = 1.0;
        zrangeTrans = 1.0;
    }

    // the origin of new bounding box, which is centered at (0.5, 0.5,0.5)
    double xoriginTrans, yoriginTrans, zoriginTrans;
    xoriginTrans = 0.5 - xrangeTrans / 2;
    yoriginTrans = 0.5 - yrangeTrans / 2;
    zoriginTrans = 0.5 - zrangeTrans / 2;

    // scale factors to unit cube
    mat4x4[0][0] = xrangeTrans / xrange;
    mat4x4[1][1] = yrangeTrans / yrange;
    mat4x4[2][2] = zrangeTrans / zrange;

    // tranlate amount
    mat4x4[3][0] = xoriginTrans - xrangeTrans * bounds[0] / xrange;
    mat4x4[3][1] = yoriginTrans - yrangeTrans * bounds[2] / yrange;
    mat4x4[3][2] = zoriginTrans - zrangeTrans * bounds[4] / zrange;

    // others are 0
    mat4x4[0][1] = 0; mat4x4[0][2] = 0; mat4x4[0][3] = 0;
    mat4x4[1][0] = 0; mat4x4[1][2] = 0; mat4x4[1][3] = 0;
    mat4x4[2][0] = 0; mat4x4[2][1] = 0; mat4x4[2][3] = 0;
    mat4x4[3][3] = 1; // the bottom-right corner has to be 1 to multiply with another transform matrix
}

void vtkSlicerSkeletalRepresentationRefinerLogic::ConnectImpliedBoundaryPts(int interpolationLevel,int nRows, int nCols,
                                                               const string &srepFileName,
                                                                            vtkPolyData *polyImpliedBoundary,
                                                                            vtkPoints *foldCurvePts, vtkCellArray *foldCurveCell,
                                                                            std::vector<vtkSpoke *> &interpolatedSpokes,
                                                                            std::vector<vtkSpoke*>& primary
                                                               )
{
    std::vector<double> coeffArray, radii, dirs, skeletalPoints;
    Parse(srepFileName, coeffArray, radii, dirs, skeletalPoints);

    if(nRows == 0 || nCols == 0)
    {
        std::cerr << "The s-rep model is empty." << std::endl;
        return;
    }

    vtkSrep *srep = new vtkSrep(nRows, nCols, radii, dirs, skeletalPoints);
    if(srep->IsEmpty())
    {
        std::cerr << "The s-rep model is empty." << std::endl;
        delete srep;
        srep = nullptr;
        return;
    }

    vtkSmartPointer<vtkPoints> pts = vtkSmartPointer<vtkPoints>::New();
    vtkSmartPointer<vtkCellArray> quads = vtkSmartPointer<vtkCellArray>::New();

    // 1.1 interpolate and visualize for verification
    // collect neighboring spokes around corners
    vtkSlicerSkeletalRepresentationInterpolater interpolater;

    int shares = static_cast<int>(pow(2, interpolationLevel));
    double interval = static_cast<double>((1.0/ shares));
    std::vector<double> steps;

    for(int i = 0; i <= shares; ++i)
    {
        steps.push_back(i * interval);
    }

    for(int r = 0; r < nRows-1; ++r)
    {
        for(int c = 0; c < nCols-1; ++c)
        {
            vtkSpoke *cornerSpokes[4];

            double  dXdu11[3], dXdv11[3],
                    dXdu12[3], dXdv12[3],
                    dXdu21[3], dXdv21[3],
                    dXdu22[3], dXdv22[3];
            cornerSpokes[0] = srep->GetSpoke(r,c);
            cornerSpokes[1] = srep->GetSpoke(r+1, c);
            cornerSpokes[2] = srep->GetSpoke(r+1, c+1);
            cornerSpokes[3] = srep->GetSpoke(r, c+ 1);

            ComputeDerivative(skeletalPoints, r, c, nRows, nCols, dXdu11, dXdv11);
            ComputeDerivative(skeletalPoints, r+1, c, nRows, nCols, dXdu21, dXdv21);
            ComputeDerivative(skeletalPoints, r, c+1, nRows, nCols, dXdu12, dXdv12);
            ComputeDerivative(skeletalPoints, r+1, c+1, nRows, nCols, dXdu22, dXdv22);

            interpolater.SetCornerDxdu(dXdu11,
                                       dXdu21,
                                       dXdu22,
                                       dXdu12);
            interpolater.SetCornerDxdv(dXdv11,
                                       dXdv21,
                                       dXdv22,
                                       dXdv12);

            std::vector<vtkSpoke *> innerQuadSpokes;
            std::vector<vtkSpoke *> topEdgeSpokes, botEdgeSpokes, leftEdgeSpokes, rightEdgeSpokes;
            for(size_t i = 0; i < steps.size(); ++i)
            {
                for(size_t j = 0; j < steps.size(); ++j)
                {
                    vtkSpoke* in1 = new vtkSpoke;
                    interpolater.Interpolate(double(steps[i]), double(steps[j]), cornerSpokes, in1);
                    interpolatedSpokes.push_back(in1);
                    innerQuadSpokes.push_back(in1);
                    if(r == 0 && i == 0)
                    {
                        topEdgeSpokes.push_back(in1);
                    }
                    if(c == 0 && j == 0)
                    {
                        leftEdgeSpokes.push_back(in1);
                    }
                    if(r == nRows - 2 && i == steps.size() - 1)
                    {
                        botEdgeSpokes.push_back(in1);
                    }
                    if(c == nCols - 2 && j == steps.size() - 1)
                    {
                        rightEdgeSpokes.push_back(in1);
                    }
                }
            }

            // quads for inner spokes
            // row
//            for(int i = 0; i < shares; ++i)
//            {
//                // col
//                for(int j = 0; j < shares; ++j)
//                {
//                    size_t idTop = static_cast<size_t>(i * (shares+1) + j);
//                    vtkSpoke *s0 = innerQuadSpokes[idTop];
//                    vtkSpoke *s1 = innerQuadSpokes[idTop+1];

//                    size_t idBot = static_cast<size_t>((i+1) * (shares + 1) + j);
//                    vtkSpoke *s2 = innerQuadSpokes[idBot];
//                    vtkSpoke *s3 = innerQuadSpokes[idBot+1];
//                    double p0[3], p1[3], p2[3], p3[3];
//                    s0->GetBoundaryPoint(p0);
//                    s1->GetBoundaryPoint(p1);
//                    s2->GetBoundaryPoint(p2);
//                    s3->GetBoundaryPoint(p3);
//                    vtkIdType id0 = pts->InsertNextPoint(p0);
//                    vtkIdType id1 = pts->InsertNextPoint(p1);
//                    vtkIdType id2 = pts->InsertNextPoint(p2);
//                    vtkIdType id3 = pts->InsertNextPoint(p3);
//                    vtkSmartPointer<vtkQuad> quad = vtkSmartPointer<vtkQuad>::New();
//                    quad->GetPointIds()->SetId(0, id0);
//                    quad->GetPointIds()->SetId(1, id2);
//                    quad->GetPointIds()->SetId(2, id3);
//                    quad->GetPointIds()->SetId(3, id1);
//                    quads->InsertNextCell(quad);

//                }

//            }

            ConnectFoldCurve(topEdgeSpokes, foldCurvePts, foldCurveCell);
            ConnectFoldCurve(botEdgeSpokes, foldCurvePts, foldCurveCell);
            ConnectFoldCurve(leftEdgeSpokes, foldCurvePts, foldCurveCell);
            ConnectFoldCurve(rightEdgeSpokes, foldCurvePts, foldCurveCell);


        }
    }

    vtkSmartPointer<vtkDoubleArray> normalsArray =
            vtkSmartPointer<vtkDoubleArray>::New();
    normalsArray->SetNumberOfComponents(3); //3d normals (ie x,y,z)
    normalsArray->SetNumberOfTuples(interpolatedSpokes.size());

    for(int i = 0; i < interpolatedSpokes.size(); ++i) {
        double normalDir[3];
        double bdry[3], skeletalPt[3];
        interpolatedSpokes[i]->GetBoundaryPoint(bdry);
        interpolatedSpokes[i]->GetSkeletalPoint(skeletalPt);
        normalDir[0] = bdry[0] - skeletalPt[0];
        normalDir[1] = bdry[1] - skeletalPt[1];
        normalDir[2] = bdry[2] - skeletalPt[2];
        normalsArray->SetTuple(i, normalDir);
        pts->InsertNextPoint(bdry);
    }

    std::vector<vtkSpoke *> pSpokes = srep->GetAllSpokes();
    for (size_t i = 0; i < pSpokes.size(); ++i) {
        vtkSpoke *s = new vtkSpoke(*pSpokes[i]);
        primary.push_back(s);
    }

    polyImpliedBoundary->SetPoints(pts);
    polyImpliedBoundary->GetPointData()->SetNormals(normalsArray);
//    polyImpliedBoundary->SetPolys(quads);
}

void vtkSlicerSkeletalRepresentationRefinerLogic::ConnectImpliedCrest(int interpolationLevel,
                                                                      int nRows, int nCols,
                                                                      const std::string &crest,
                                                                      std::vector<vtkSpoke*> &upSpokes,
                                                                      std::vector<vtkSpoke*> &downSpokes,
                                                                      vtkAppendPolyData* output)
{
    std::vector<vtkSpoke*> crestSpokes, topCrest;
    ParseCrest(crest, crestSpokes);

    std::vector<vtkSpoke *> upInterpSpokes, downInterpSpokes, crestInterpSpokes, tempInterp, reorderedCrest;
    ReorderCrestSpokes(nRows, nCols, crestSpokes, reorderedCrest);
    InterpolateCrest(reorderedCrest, upSpokes, interpolationLevel, nRows, nCols, crestInterpSpokes, upInterpSpokes);

    InterpolateCrest(reorderedCrest, downSpokes, interpolationLevel, nRows, nCols, tempInterp, downInterpSpokes);

    vtkSmartPointer<vtkPolyData> upInterpSpokesPoly = vtkSmartPointer<vtkPolyData>::New();
    ConvertSpokes2PolyData(upInterpSpokes, upInterpSpokesPoly);
    Visualize(upInterpSpokesPoly, "up spokes", 0, 0,0, false);

    vtkSmartPointer<vtkPolyData> downInterpSpokesPoly = vtkSmartPointer<vtkPolyData>::New();
    ConvertSpokes2PolyData(downInterpSpokes, downInterpSpokesPoly);
    Visualize(downInterpSpokesPoly, "down spokes", 0, 0,0, false);

    vtkSmartPointer<vtkPolyData> crestInterpSpokesPoly = vtkSmartPointer<vtkPolyData>::New();
    ConvertSpokes2PolyData(crestInterpSpokes, crestInterpSpokesPoly);
    Visualize(crestInterpSpokesPoly, "crest spokes", 0, 0,0, false);

    // shares between up spoke to resp. down spoke
    int shares = 2 * static_cast<int>(pow(2, interpolationLevel));
    double interval = static_cast<double>((1.0/ shares));

    vtkSmartPointer<vtkPoints> crestPoints = vtkSmartPointer<vtkPoints>::New();
    vtkSmartPointer<vtkAppendPolyData> appendFilter =
      vtkSmartPointer<vtkAppendPolyData>::New();
    double ptCrest[3], ptUp[3], ptDown[3], ptInterp[3], ptSkeletal[3], du[9];

    vtkSmartPointer<vtkPolyData> interpS = vtkSmartPointer<vtkPolyData>::New();
    vtkSmartPointer<vtkCellArray> interpCell = vtkSmartPointer<vtkCellArray>::New();

    vtkSmartPointer<vtkPolyData> interpDownS = vtkSmartPointer<vtkPolyData>::New();
    vtkSmartPointer<vtkCellArray> interpDownCell = vtkSmartPointer<vtkCellArray>::New();

    vtkSmartPointer<vtkPolyData> interpCrestS = vtkSmartPointer<vtkPolyData>::New();
    vtkSmartPointer<vtkCellArray> interpCrestCell = vtkSmartPointer<vtkCellArray>::New();
    vtkSmartPointer<vtkPoints> interpSPts = vtkSmartPointer<vtkPoints>::New();
    vtkSmartPointer<vtkPoints> interpSBrdy = vtkSmartPointer<vtkPoints>::New();
    for(size_t i = 0; i < crestInterpSpokes.size(); ++i)
    {
        crestInterpSpokes[i]->GetBoundaryPoint(ptCrest);
        crestPoints->InsertNextPoint(ptCrest);
        vtkSmartPointer<vtkPoints> radialCurve = vtkSmartPointer<vtkPoints>::New();
        upInterpSpokes[i]->GetBoundaryPoint(ptUp);
        downInterpSpokes[i]->GetBoundaryPoint(ptDown);
        radialCurve->InsertNextPoint(ptUp);
        radialCurve->InsertNextPoint(ptCrest);
        radialCurve->InsertNextPoint(ptDown);
        vtkSmartPointer<vtkParametricSpline> splineRadial =
                vtkSmartPointer<vtkParametricSpline>::New();
        splineRadial->SetPoints(radialCurve);
        vtkSmartPointer<vtkParametricFunctionSource> functionSourceRadial =
                vtkSmartPointer<vtkParametricFunctionSource>::New();
        functionSourceRadial->SetParametricFunction(splineRadial);
        functionSourceRadial->Update();
        // share the base point among all other interpolated spokes
        crestInterpSpokes[i]->GetSkeletalPoint(ptSkeletal);

        // interpolate along the spline
        for(int j = 1; j < shares; ++j)
        {
            double uInterp = j * interval;
            double u[3] = {uInterp, uInterp, uInterp};
            splineRadial->Evaluate(u, ptInterp, du);
            vtkIdType id0 = interpSPts->InsertNextPoint(ptInterp);
            vtkIdType id1 = interpSPts->InsertNextPoint(ptSkeletal);
            interpSBrdy->InsertNextPoint(ptInterp);
            vtkSmartPointer<vtkLine> interpSLine = vtkSmartPointer<vtkLine>::New();
            interpSLine->GetPointIds()->SetId(0, id0);
            interpSLine->GetPointIds()->SetId(1, id1);
            interpCell->InsertNextCell(interpSLine);
        }
        appendFilter->AddInputData(functionSourceRadial->GetOutput());
    }
    interpS->SetPoints(interpSPts);
    interpS->SetPolys(interpCell);
    Visualize(interpS, "Interpolated", 0, 0, 1);

    double bounds[6];
    interpS->GetBounds(bounds);

    // connect points along crest. There should be #share-1 curves in total
    for(int i = 0; i < shares; ++i)
    {
        vtkSmartPointer<vtkParametricSpline> splineAlongCrest =
                vtkSmartPointer<vtkParametricSpline>::New();
        vtkSmartPointer<vtkPoints> crestSplinePts = vtkSmartPointer<vtkPoints>::New();

        for(int j = 0; j < interpSBrdy->GetNumberOfPoints(); ++j)
        {
            if((j+1) % (shares-1) != i)
            {
                continue;
            }
            double pt[3];
            interpSBrdy->GetPoint(j, pt);
            crestSplinePts->InsertNextPoint(pt);
        }
        if(crestSplinePts->GetNumberOfPoints() > 0)
        {
            splineAlongCrest->SetPoints(crestSplinePts);
            vtkSmartPointer<vtkParametricFunctionSource> functionSourceAlong =
                    vtkSmartPointer<vtkParametricFunctionSource>::New();
            functionSourceAlong->SetParametricFunction(splineAlongCrest);
            functionSourceAlong->Update();
            appendFilter->AddInputData(functionSourceAlong->GetOutput());
        }

    }
    vtkSmartPointer<vtkParametricSpline> splineCrest =
            vtkSmartPointer<vtkParametricSpline>::New();
    splineCrest->SetPoints(crestPoints);
    vtkSmartPointer<vtkParametricFunctionSource> functionSourceCrest =
            vtkSmartPointer<vtkParametricFunctionSource>::New();
    functionSourceCrest->SetParametricFunction(splineCrest);
    functionSourceCrest->Update();
    appendFilter->AddInputData(functionSourceCrest->GetOutput());
    appendFilter->Update();

    // Remove any duplicate points.
    vtkSmartPointer<vtkCleanPolyData> cleanFilter =
            vtkSmartPointer<vtkCleanPolyData>::New();
    cleanFilter->SetInputConnection(appendFilter->GetOutputPort());
    cleanFilter->Update();
    vtkSmartPointer<vtkPolyData> crestConnectPoly = cleanFilter->GetOutput();
    Visualize(crestConnectPoly, "Implied crest", 0, 1, 1);
    output->AddInputData(crestConnectPoly);
}
void vtkSlicerSkeletalRepresentationRefinerLogic::ConnectFoldCurve(const std::vector<vtkSpoke *> &edgeSpokes,
                                                                   vtkPoints *foldCurvePts, vtkCellArray *foldCurveCell)
{
    if(edgeSpokes.empty())
    {
        return;
    }
    vtkIdType id1 = 0;
    for (size_t i = 0; i < edgeSpokes.size()-1; ++i) {
        double pt0[3], pt1[3];
        edgeSpokes[i]->GetSkeletalPoint(pt0);
        edgeSpokes[i+1]->GetSkeletalPoint(pt1);
        vtkIdType id0 = foldCurvePts->InsertNextPoint(pt0);
        id1 = foldCurvePts->InsertNextPoint(pt1);

        vtkSmartPointer<vtkLine> line = vtkSmartPointer<vtkLine>::New();
        line->GetPointIds()->SetId(0, id0);
        line->GetPointIds()->SetId(1, id1);
        foldCurveCell->InsertNextCell(line);
    }
    // connect first and last point to close this curve
    vtkSmartPointer<vtkLine> line = vtkSmartPointer<vtkLine>::New();
    line->GetPointIds()->SetId(0, id1);
    line->GetPointIds()->SetId(1, 0);
    foldCurveCell->InsertNextCell(line);
}

std::vector<vtkSpoke*>& vtkSlicerSkeletalRepresentationRefinerLogic::RefinePartOfSpokes(const string &srepFileName, double stepSize, double endCriterion, int maxIter)
{
    mCoeffArray.clear();
    std::vector<double> radii, dirs, skeletalPoints;
    Parse(srepFileName, mCoeffArray, radii, dirs, skeletalPoints);

    vtkSrep *srep = new vtkSrep(mNumRows, mNumCols, radii, dirs, skeletalPoints);
    if(srep->IsEmpty())
    {
        std::cerr << "The s-rep model is empty." << std::endl;
        delete srep;
        srep = nullptr;
        std::vector<vtkSpoke*> emptyRet;
        return emptyRet;
    }

    // total number of parameters that need to optimize
    size_t paramDim = mCoeffArray.size();
    double coeff[paramDim];
    for(size_t i = 0; i < paramDim; ++i)
    {
        coeff[i] = mCoeffArray[i];
    }

    mSrep = srep;
    vtkSmartPointer<vtkPolyData> origSrep = vtkSmartPointer<vtkPolyData>::New();
    ConvertSpokes2PolyData(srep->GetAllSpokes(), origSrep);

    Visualize(origSrep, "Before refinement", 1, 0, 0);

    mFirstCost = true;
    // 2. Invoke newuoa to optimize
    min_newuoa(static_cast<int>(paramDim), coeff, *this, stepSize, endCriterion, maxIter);

    // Re-evaluate the cost
    mFirstCost = true;
    EvaluateObjectiveFunction(coeff);

    // 3. Visualize the refined srep
    srep->Refine(coeff);
    vtkSmartPointer<vtkPolyData> refinedSrep = vtkSmartPointer<vtkPolyData>::New();
    ConvertSpokes2PolyData(srep->GetAllSpokes(), refinedSrep);
    Visualize(refinedSrep, "Refined interior spokes", 0, 1, 1);

    // write to vtp file
    std::string outputFile(mOutputPath);
    std::string fileName = vtksys::SystemTools::GetFilenameName(srepFileName);
    outputFile = outputFile + newFilePrefix + fileName;
    SaveSpokes2Vtp(srep->GetAllSpokes(), outputFile);
    return srep->GetAllSpokes();
}

void vtkSlicerSkeletalRepresentationRefinerLogic::RefineCrestSpokes(const string &crest, double stepSize,
                                                                    double vtkNotUsed(endCriterion), int maxIter)
{
    // Show original crest spokes
    std::vector<vtkSpoke*> crestSpokes, topCrest;
    ParseCrest(crest, crestSpokes);

    vtkSmartPointer<vtkPolyData> crestSrep = vtkSmartPointer<vtkPolyData>::New();
    ConvertSpokes2PolyData(crestSpokes, crestSrep);

    Visualize(crestSrep, "Crest before refinement", 1, 1, 0);

    vtkSmartPointer<vtkPolyDataReader> meshReader = vtkSmartPointer<vtkPolyDataReader>::New();
    meshReader->SetFileName(mTargetMeshFilePath.c_str());
    meshReader->Update();
    vtkSmartPointer<vtkPolyData> mesh = meshReader->GetOutput();
    vtkSmartPointer<vtkImplicitPolyDataDistance> implicitPolyDataDistance =
      vtkSmartPointer<vtkImplicitPolyDataDistance>::New();
    implicitPolyDataDistance->SetInput(mesh);
    for (size_t i = 0; i < crestSpokes.size(); ++i) {
        OptimizeCrestSpokeLength(implicitPolyDataDistance, crestSpokes[i], stepSize, maxIter);
    }
    // set crest radii to the reciprocal of crest curvature
    vtkSmartPointer<vtkCurvatures> curvaturesFilter =
        vtkSmartPointer<vtkCurvatures>::New();
    curvaturesFilter->SetInputData(mesh);
    curvaturesFilter->SetCurvatureTypeToMaximum();
    curvaturesFilter->Update();

    vtkSmartPointer<vtkDoubleArray> MC =
        vtkDoubleArray::SafeDownCast(curvaturesFilter->GetOutput()->GetPointData()->GetArray("Maximum_Curvature"));

    if(MC == nullptr) {
        std::cerr << "error in getting max curvature" << std::endl;
        return;
    }

    curvaturesFilter->SetCurvatureTypeToMinimum();
    curvaturesFilter->Update();

    vtkSmartPointer<vtkDoubleArray> MinC =
        vtkDoubleArray::SafeDownCast(curvaturesFilter->GetOutput()->GetPointData()->GetArray("Minimum_Curvature"));
    if(MinC == nullptr)
    {
        std::cout << "error in getting min curvature" << std::endl;
        return;
    }
    // find the nearest point id on the mesh
    vtkSmartPointer<vtkPointLocator> locator = vtkPointLocator::New();
    locator->SetDataSet(mesh);
    locator->BuildLocator();
    for (size_t i = 0; i < crestSpokes.size(); ++i) {
        double bdryPt[3];
        crestSpokes[i]->GetBoundaryPoint(bdryPt);
        vtkIdType idNearest = locator->FindClosestPoint(bdryPt);
        double curr_max = MC->GetValue(idNearest);
        double curr_min = MinC->GetValue(idNearest);
        double rCrest = 1 / max(abs(curr_max), abs(curr_min));
        double rDiff = crestSpokes[i]->GetRadius() - rCrest;
        if(rDiff <= 0) continue;
        // move skeletal point of this crest outward by rDiff
        double skeletalPt[3], u[3];
        crestSpokes[i]->GetDirection(u);
        crestSpokes[i]->GetSkeletalPoint(skeletalPt);
        crestSpokes[i]->SetSkeletalPoint(skeletalPt[0] + u[0] * rDiff, skeletalPt[1] + u[1] * rDiff, skeletalPt[2] + u[2] * rDiff);
        crestSpokes[i]->SetRadius(rCrest);
    }
    // 3. Visualize the refined srep
    vtkSmartPointer<vtkPolyData> refinedSrep = vtkSmartPointer<vtkPolyData>::New();
    ConvertSpokes2PolyData(crestSpokes, refinedSrep);
    Visualize(refinedSrep, "Refined crest", 0, 1, 1);

    // write to vtp file
    std::string outputFile(mOutputPath);
    std::string fileName = vtksys::SystemTools::GetFilenameName(crest);
    outputFile = outputFile + newFilePrefix + fileName;
    SaveSpokes2Vtp(crestSpokes, outputFile);
    if(mSrep != nullptr)
    {
        delete mSrep;
        mSrep = nullptr;
    }
}

double vtkSlicerSkeletalRepresentationRefinerLogic::TotalDistOfLeftTopSpoke(vtkSrep *tempSrep,
                                                                            double u, double v,
                                                                            int r, int c,
                                                                            double *normalMatch)
{
    vtkSlicerSkeletalRepresentationInterpolater interpolater;
    vtkSpoke *cornerSpokes[4];
    double imageDist = 0.0;
    cornerSpokes[0] = tempSrep->GetSpoke(r, c);
    cornerSpokes[1] = tempSrep->GetSpoke(r+1, c);
    cornerSpokes[2] = tempSrep->GetSpoke(r+1, c+1);
    cornerSpokes[3] = tempSrep->GetSpoke(r, c+1);
    double  dXdu11[3], dXdv11[3],
            dXdu12[3], dXdv12[3],
            dXdu21[3], dXdv21[3],
            dXdu22[3], dXdv22[3];
    std::vector<double> skeletalPts = tempSrep->GetAllSkeletalPoints();
    int nRows = tempSrep->GetNumRows();
    int nCols = tempSrep->GetNumCols();
    ComputeDerivative(skeletalPts, r, c, nRows, nCols, dXdu11, dXdv11);
    ComputeDerivative(skeletalPts, r+1, c, nRows, nCols, dXdu21, dXdv21);
    ComputeDerivative(skeletalPts, r+1, c+1, nRows, nCols, dXdu22, dXdv22);
    ComputeDerivative(skeletalPts, r, c+1, nRows, nCols, dXdu12, dXdv12);

    interpolater.SetCornerDxdu(dXdu11,
                               dXdu21,
                               dXdu22,
                               dXdu12);
    interpolater.SetCornerDxdv(dXdv11,
                               dXdv21,
                               dXdv22,
                               dXdv12);
    vtkSpoke interpolatedSpoke;
    interpolater.Interpolate(u, v, cornerSpokes, &interpolatedSpoke);

    // compute the ssd for this interpolated spoke
    imageDist += ComputeDistance(&interpolatedSpoke, normalMatch);

    return imageDist;
}

double vtkSlicerSkeletalRepresentationRefinerLogic::TotalDistOfRightTopSpoke(vtkSrep *tempSrep,
                                                                             double u, double v,
                                                                             int r, int c,
                                                                             double *normalMatch)
{
    vtkSlicerSkeletalRepresentationInterpolater interpolater;
    vtkSpoke *cornerSpokes[4];
    double imageDist = 0.0;
    cornerSpokes[0] = tempSrep->GetSpoke(r, c-1);
    cornerSpokes[1] = tempSrep->GetSpoke(r+1, c-1);
    cornerSpokes[2] = tempSrep->GetSpoke(r+1, c);
    cornerSpokes[3] = tempSrep->GetSpoke(r, c);
    double  dXdu11[3], dXdv11[3],
            dXdu12[3], dXdv12[3],
            dXdu21[3], dXdv21[3],
            dXdu22[3], dXdv22[3];
    std::vector<double> skeletalPts = tempSrep->GetAllSkeletalPoints();
    int nRows = tempSrep->GetNumRows();
    int nCols = tempSrep->GetNumCols();
    ComputeDerivative(skeletalPts, r, c-1, nRows, nCols, dXdu11, dXdv11);
    ComputeDerivative(skeletalPts, r+1, c-1, nRows, nCols, dXdu21, dXdv21);
    ComputeDerivative(skeletalPts, r+1, c, nRows, nCols, dXdu22, dXdv22);
    ComputeDerivative(skeletalPts, r, c, nRows, nCols, dXdu12, dXdv12);

    interpolater.SetCornerDxdu(dXdu11,
                               dXdu21,
                               dXdu22,
                               dXdu12);
    interpolater.SetCornerDxdv(dXdv11,
                               dXdv21,
                               dXdv22,
                               dXdv12);
    vtkSpoke interpolatedSpoke;
    interpolater.Interpolate(u, v, cornerSpokes, &interpolatedSpoke);

    // compute the ssd & normal match for this interpolated spoke
    imageDist += ComputeDistance(&interpolatedSpoke, normalMatch);
    return imageDist;
}

double vtkSlicerSkeletalRepresentationRefinerLogic::TotalDistOfLeftBotSpoke(vtkSrep *tempSrep,
                                                                            double u, double v,
                                                                            int r, int c,
                                                                            double *normalMatch)
{
    vtkSlicerSkeletalRepresentationInterpolater interpolater;
    vtkSpoke *cornerSpokes[4];
    double imageDist = 0.0;
    cornerSpokes[0] = tempSrep->GetSpoke(r-1, c);
    cornerSpokes[1] = tempSrep->GetSpoke(r, c);
    cornerSpokes[2] = tempSrep->GetSpoke(r, c+1);
    cornerSpokes[3] = tempSrep->GetSpoke(r-1, c+1);
    double  dXdu11[3], dXdv11[3],
            dXdu12[3], dXdv12[3],
            dXdu21[3], dXdv21[3],
            dXdu22[3], dXdv22[3];
    std::vector<double> skeletalPts = tempSrep->GetAllSkeletalPoints();
    int nRows = tempSrep->GetNumRows();
    int nCols = tempSrep->GetNumCols();
    ComputeDerivative(skeletalPts, r-1, c, nRows, nCols, dXdu11, dXdv11);
    ComputeDerivative(skeletalPts, r, c, nRows, nCols, dXdu21, dXdv21);
    ComputeDerivative(skeletalPts, r, c+1, nRows, nCols, dXdu22, dXdv22);
    ComputeDerivative(skeletalPts, r-1, c+1, nRows, nCols, dXdu12, dXdv12);

    interpolater.SetCornerDxdu(dXdu11,
                               dXdu21,
                               dXdu22,
                               dXdu12);
    interpolater.SetCornerDxdv(dXdv11,
                               dXdv21,
                               dXdv22,
                               dXdv12);
    vtkSpoke interpolatedSpoke;
    interpolater.Interpolate(u, v, cornerSpokes, &interpolatedSpoke);

    // compute the ssd for this interpolated spoke
    imageDist += ComputeDistance(&interpolatedSpoke, normalMatch);
    return imageDist;
}

double vtkSlicerSkeletalRepresentationRefinerLogic::TotalDistOfRightBotSpoke(vtkSrep *tempSrep,
                                                                             double u, double v,
                                                                             int r, int c,
                                                                             double *normalMatch)
{
    vtkSlicerSkeletalRepresentationInterpolater interpolater;
    vtkSpoke *cornerSpokes[4];
    double imageDist = 0.0;
    cornerSpokes[0] = tempSrep->GetSpoke(r-1, c-1);
    cornerSpokes[1] = tempSrep->GetSpoke(r, c-1);
    cornerSpokes[2] = tempSrep->GetSpoke(r, c);
    cornerSpokes[3] = tempSrep->GetSpoke(r-1, c);
    double  dXdu11[3], dXdv11[3],
            dXdu12[3], dXdv12[3],
            dXdu21[3], dXdv21[3],
            dXdu22[3], dXdv22[3];
    std::vector<double> skeletalPts = tempSrep->GetAllSkeletalPoints();
    int nRows = tempSrep->GetNumRows();
    int nCols = tempSrep->GetNumCols();
    ComputeDerivative(skeletalPts, r-1, c-1, nRows, nCols, dXdu11, dXdv11);
    ComputeDerivative(skeletalPts, r, c-1, nRows, nCols, dXdu21, dXdv21);
    ComputeDerivative(skeletalPts, r, c, nRows, nCols, dXdu22, dXdv22);
    ComputeDerivative(skeletalPts, r-1, c, nRows, nCols, dXdu12, dXdv12);

    interpolater.SetCornerDxdu(dXdu11,
                               dXdu21,
                               dXdu22,
                               dXdu12);
    interpolater.SetCornerDxdv(dXdv11,
                               dXdv21,
                               dXdv22,
                               dXdv12);
    vtkSpoke interpolatedSpoke;
    interpolater.Interpolate(u, v, cornerSpokes, &interpolatedSpoke);

    // compute the ssd for this interpolated spoke
    imageDist += ComputeDistance(&interpolatedSpoke, normalMatch);
    return imageDist;
}

double vtkSlicerSkeletalRepresentationRefinerLogic::ComputeRSradPenalty(vtkSrep *input)
{
    double penalty = 0.0;
    // Interpolate
    if(input->IsEmpty())
    {
        std::cerr << "The s-rep model is empty in computing rSrad." << std::endl;
        return 0.0;
    }
    // 1.1 interpolate and visualize for verification
    // collect neighboring spokes around corners
    vtkSlicerSkeletalRepresentationInterpolater interpolater;
    int nRows = input->GetNumRows();
    int nCols = input->GetNumCols();

    std::vector<vtkSpoke*> interpolatedSpokes;
    std::vector<vtkSpoke*> primarySpokes;
    for(int r = 0; r < nRows; ++r)
    {
        for(int c = 0; c < nCols; ++c)
        {
            vtkSpoke* thisSpoke = input->GetSpoke(r, c);

            std::vector<vtkSpoke*> neighborU, neighborV;
            if(r == 0 && c == 0)
            {
                // top left corner
                FindTopLeftNeigbors(r, c, input, neighborU, neighborV);
                thisSpoke->SetNeighborU(neighborU, true);
                thisSpoke->SetNeighborV(neighborV, true);
            }
            else if(r == 0 && c == nCols - 1)
            {
                // top right corner
                FindTopRightNeigbors(r, c, input, neighborU, neighborV);
                thisSpoke->SetNeighborU(neighborU, true);
                thisSpoke->SetNeighborV(neighborV, false);
            }
            else if(r == 0)
            {
                // top edge
                FindTopRightNeigbors(r, c, input, neighborU, neighborV);
                FindTopLeftNeigbors(r, c, input, neighborU, neighborV);
                neighborU.pop_back();
                thisSpoke->SetNeighborU(neighborU, true);
                thisSpoke->SetNeighborV(neighborV, false);
            }
            else if(r == nRows - 1 && c == 0)
            {
                // left bot corner
                FindBotLeftNeigbors(r, c, input, neighborU, neighborV);
                thisSpoke->SetNeighborU(neighborU, false);
                thisSpoke->SetNeighborV(neighborV, true);
            }
            else if(r == nRows - 1 && c == nCols -1)
            {
                // right bot corner
                FindBotRightNeigbors(r, c, input, neighborU, neighborV);
                thisSpoke->SetNeighborU(neighborU, false);
                thisSpoke->SetNeighborV(neighborV, false);
            }
            else if(r == nRows - 1)
            {
                // bot edge
                FindBotRightNeigbors(r, c, input, neighborU, neighborV);
                FindBotLeftNeigbors(r, c, input, neighborU, neighborV);
                neighborU.pop_back();
                thisSpoke->SetNeighborU(neighborU, false);
                thisSpoke->SetNeighborV(neighborV, false);
            }
            else if(c == 0)
            {
                // left edge
                FindBotLeftNeigbors(r, c, input, neighborU, neighborV);
                FindTopLeftNeigbors(r, c, input, neighborU, neighborV);
                neighborV.pop_back();
                thisSpoke->SetNeighborU(neighborU, false);
                thisSpoke->SetNeighborV(neighborV, true);
            }
            else if(c == nCols - 1)
            {
                // right edge
                FindBotRightNeigbors(r, c, input, neighborU, neighborV);
                FindTopRightNeigbors(r, c, input, neighborU, neighborV);
                neighborV.pop_back();
                thisSpoke->SetNeighborU(neighborU, false);
                thisSpoke->SetNeighborV(neighborV, false);
            }
            else {
                // interior
                FindBotRightNeigbors(r, c, input, neighborU, neighborV);
                FindTopLeftNeigbors(r, c, input, neighborU, neighborV);
                thisSpoke->SetNeighborU(neighborU, false);
                thisSpoke->SetNeighborV(neighborV, false);
            }

            primarySpokes.push_back(thisSpoke);
        }

    }

    //compute the penalty
    double step = mInterpolatePositions[0].second;
    for(size_t i = 0; i < primarySpokes.size(); ++i)
    {
        double thisPenalty = primarySpokes[i]->GetRSradPenalty(step);
        penalty += thisPenalty;
    }
    return penalty;
}

void vtkSlicerSkeletalRepresentationRefinerLogic::FindTopLeftNeigbors(int r, int c,
                                                               vtkSrep* input,
                                                               std::vector<vtkSpoke *> &neighborU,
                                                               std::vector<vtkSpoke *> &neighborV)
{
    vtkSlicerSkeletalRepresentationInterpolater interpolater;
    int nRows = input->GetNumRows();
    int nCols = input->GetNumCols();
    vtkSpoke *cornerSpokes[4];

    double  dXdu11[3], dXdv11[3],
            dXdu12[3], dXdv12[3],
            dXdu21[3], dXdv21[3],
            dXdu22[3], dXdv22[3];
    cornerSpokes[0] = input->GetSpoke(r, c);
    cornerSpokes[1] = input->GetSpoke(r+1, c);
    cornerSpokes[2] = input->GetSpoke(r+1, c+1);
    cornerSpokes[3] = input->GetSpoke(r, c+ 1);
    std::vector<double> skeletalPts = input->GetAllSkeletalPoints();
    ComputeDerivative(skeletalPts, r, c, nRows, nCols, dXdu11, dXdv11);
    ComputeDerivative(skeletalPts, r+1, c, nRows, nCols, dXdu21, dXdv21);
    ComputeDerivative(skeletalPts, r, c+1, nRows, nCols, dXdu12, dXdv12);
    ComputeDerivative(skeletalPts, r+1, c+1, nRows, nCols, dXdu22, dXdv22);

    interpolater.SetCornerDxdu(dXdu11,
                               dXdu21,
                               dXdu22,
                               dXdu12);
    interpolater.SetCornerDxdv(dXdv11,
                               dXdv21,
                               dXdv22,
                               dXdv12);

    vtkSpoke* in1 = new vtkSpoke;
    vtkSpoke* in2 = new vtkSpoke;
    double stepV = mInterpolatePositions[0].second;
    double stepU = stepV;
    interpolater.Interpolate(stepU, 0, cornerSpokes, in1);
    neighborU.push_back(in1);
    interpolater.Interpolate(0, stepV, cornerSpokes, in2);
    neighborV.push_back(in2);
}

void vtkSlicerSkeletalRepresentationRefinerLogic::FindTopRightNeigbors(int r, int c, vtkSrep *input, std::vector<vtkSpoke *> &neighborU, std::vector<vtkSpoke *> &neighborV)
{
    vtkSlicerSkeletalRepresentationInterpolater interpolater;
    int nRows = input->GetNumRows();
    int nCols = input->GetNumCols();
    vtkSpoke *cornerSpokes[4];
    std::vector<double> skeletalPts = input->GetAllSkeletalPoints();
    double  dXdu11[3], dXdv11[3],
            dXdu12[3], dXdv12[3],
            dXdu21[3], dXdv21[3],
            dXdu22[3], dXdv22[3];
    cornerSpokes[0] = input->GetSpoke(r, c- 1);
    cornerSpokes[1] = input->GetSpoke(r+1, c-1);
    cornerSpokes[2] = input->GetSpoke(r+1, c);
    cornerSpokes[3] = input->GetSpoke(r, c);
    ComputeDerivative(skeletalPts, r, c-1, nRows, nCols, dXdu11, dXdv11);
    ComputeDerivative(skeletalPts, r+1, c-1, nRows, nCols, dXdu21, dXdv21);
    ComputeDerivative(skeletalPts, r+1, c, nRows, nCols, dXdu12, dXdv12);
    ComputeDerivative(skeletalPts, r, c, nRows, nCols, dXdu22, dXdv22);

    interpolater.SetCornerDxdu(dXdu11,
                               dXdu21,
                               dXdu22,
                               dXdu12);
    interpolater.SetCornerDxdv(dXdv11,
                               dXdv21,
                               dXdv22,
                               dXdv12);

    vtkSpoke* in1 = new vtkSpoke;
    vtkSpoke* in2 = new vtkSpoke;
    double stepV = mInterpolatePositions[0].second;
    double stepU = stepV;
    interpolater.Interpolate(stepU, 1, cornerSpokes, in1);
    neighborU.push_back(in1);
    interpolater.Interpolate(0, 1-stepV, cornerSpokes, in2);
    neighborV.push_back(in2);
}

void vtkSlicerSkeletalRepresentationRefinerLogic::FindBotLeftNeigbors(int r, int c,
                                                                      vtkSrep *input,
                                                                      std::vector<vtkSpoke *> &neighborU,
                                                                      std::vector<vtkSpoke *> &neighborV)
{
    vtkSlicerSkeletalRepresentationInterpolater interpolater;
    int nRows = input->GetNumRows();
    int nCols = input->GetNumCols();
    vtkSpoke *cornerSpokes[4];
    std::vector<double> skeletalPts = input->GetAllSkeletalPoints();
    double  dXdu11[3], dXdv11[3],
            dXdu12[3], dXdv12[3],
            dXdu21[3], dXdv21[3],
            dXdu22[3], dXdv22[3];
    cornerSpokes[0] = input->GetSpoke(r-1, c);
    cornerSpokes[1] = input->GetSpoke(r, c);
    cornerSpokes[2] = input->GetSpoke(r, c+1);
    cornerSpokes[3] = input->GetSpoke(r-1, c+1);
    ComputeDerivative(skeletalPts, r-1, c, nRows, nCols, dXdu11, dXdv11);
    ComputeDerivative(skeletalPts, r, c, nRows, nCols, dXdu21, dXdv21);
    ComputeDerivative(skeletalPts, r, c+1, nRows, nCols, dXdu12, dXdv12);
    ComputeDerivative(skeletalPts, r-1, c+1, nRows, nCols, dXdu22, dXdv22);

    interpolater.SetCornerDxdu(dXdu11,
                               dXdu21,
                               dXdu22,
                               dXdu12);
    interpolater.SetCornerDxdv(dXdv11,
                               dXdv21,
                               dXdv22,
                               dXdv12);

    vtkSpoke* in1 = new vtkSpoke;
    vtkSpoke* in2 = new vtkSpoke;
    double stepV = mInterpolatePositions[0].second;
    double stepU = stepV;
    interpolater.Interpolate(1-stepU, 0, cornerSpokes, in1);
    neighborU.push_back(in1);
    interpolater.Interpolate(0, stepV, cornerSpokes, in2);
    neighborV.push_back(in2);
}

void vtkSlicerSkeletalRepresentationRefinerLogic::FindBotRightNeigbors(int r, int c,
                                                                       vtkSrep *input,
                                                                       std::vector<vtkSpoke *> &neighborU,
                                                                       std::vector<vtkSpoke *> &neighborV)
{
    vtkSlicerSkeletalRepresentationInterpolater interpolater;
    int nRows = input->GetNumRows();
    int nCols = input->GetNumCols();
    vtkSpoke *cornerSpokes[4];
    std::vector<double> skeletalPts = input->GetAllSkeletalPoints();
    double  dXdu11[3], dXdv11[3],
            dXdu12[3], dXdv12[3],
            dXdu21[3], dXdv21[3],
            dXdu22[3], dXdv22[3];
    cornerSpokes[0] = input->GetSpoke(r-1, c-1);
    cornerSpokes[1] = input->GetSpoke(r, c-1);
    cornerSpokes[2] = input->GetSpoke(r, c);
    cornerSpokes[3] = input->GetSpoke(r-1, c);
    ComputeDerivative(skeletalPts, r-1, c-1, nRows, nCols, dXdu11, dXdv11);
    ComputeDerivative(skeletalPts, r, c-1, nRows, nCols, dXdu21, dXdv21);
    ComputeDerivative(skeletalPts, r, c, nRows, nCols, dXdu12, dXdv12);
    ComputeDerivative(skeletalPts, r-1, c, nRows, nCols, dXdu22, dXdv22);

    interpolater.SetCornerDxdu(dXdu11,
                               dXdu21,
                               dXdu22,
                               dXdu12);
    interpolater.SetCornerDxdv(dXdv11,
                               dXdv21,
                               dXdv22,
                               dXdv12);

    vtkSpoke* in1 = new vtkSpoke;
    vtkSpoke* in2 = new vtkSpoke;
    double stepV = mInterpolatePositions[0].second;
    double stepU = stepV;
    interpolater.Interpolate(1-stepU, 1, cornerSpokes, in1);
    neighborU.push_back(in1);
    interpolater.Interpolate(1, 1-stepV, cornerSpokes, in2);
    neighborV.push_back(in2);
}
void vtkSlicerSkeletalRepresentationRefinerLogic::ParseCrest(const string &crestFileName, std::vector<vtkSpoke*> &crestSpokes)
{
    vtkSmartPointer<vtkXMLPolyDataReader> reader = vtkSmartPointer<vtkXMLPolyDataReader>::New();
    reader->SetFileName(crestFileName.c_str());
    reader->Update();

    vtkSmartPointer<vtkPolyData> spokesPolyData = reader->GetOutput();
    vtkSmartPointer<vtkPointData> spokesPointData = spokesPolyData->GetPointData();
    int numOfArrays = spokesPointData->GetNumberOfArrays();
    vtkIdType numOfSpokes = spokesPolyData->GetNumberOfPoints();

    if(numOfSpokes == 0 || numOfArrays == 0)
    {
        return;
    }

    // including Ux, Uy, Uz, r
    vtkSmartPointer<vtkDoubleArray> spokeRadii = vtkDoubleArray::SafeDownCast(spokesPointData->GetArray("spokeLength"));
    vtkSmartPointer<vtkDoubleArray> spokeDirs = vtkDoubleArray::SafeDownCast(spokesPointData->GetArray("spokeDirection"));

    for(int i = 0; i < numOfSpokes; ++i)
    {
        int idxDir = i * 3; // Ux, Uy, Uz

        vtkSpoke* crestSpoke = new vtkSpoke;
        crestSpoke->SetRadius(spokeRadii->GetValue(i));
        double u[3];
        u[0] = spokeDirs->GetValue(idxDir+0); u[1] = spokeDirs->GetValue(idxDir+1); u[2] = spokeDirs->GetValue(idxDir+2);
        crestSpoke->SetDirection(u);

        double tempSkeletalPoint[3];
        spokesPolyData->GetPoint(i, tempSkeletalPoint);
        crestSpoke->SetSkeletalPoint(tempSkeletalPoint[0], tempSkeletalPoint[1], tempSkeletalPoint[2]);
        crestSpokes.push_back(crestSpoke);
    }
}

// interpolate the crest along clock-wise direction
void vtkSlicerSkeletalRepresentationRefinerLogic::InterpolateCrest(std::vector<vtkSpoke *> &crestSpoke,
                                                                   std::vector<vtkSpoke *> &interiorSpokes,
                                                                   int interpolationLevel, int nRows,
                                                                   int nCols, std::vector<vtkSpoke*> &crest,
                                                                   std::vector<vtkSpoke*> &interior)
{
    std::vector<double> skeletalPts;
       for(size_t i = 0; i < interiorSpokes.size(); ++i)
       {
           double pt[3];
           interiorSpokes[i]->GetSkeletalPoint(pt);
           skeletalPts.push_back(pt[0]);
           skeletalPts.push_back(pt[1]);
           skeletalPts.push_back(pt[2]);
       }
       vtkSlicerSkeletalRepresentationInterpolater interpolater;

       int shares = static_cast<int>(pow(2, interpolationLevel));
       double interval = static_cast<double>((1.0/ shares));
       std::vector<double> steps;

       for(int i = 0; i <= shares; ++i)
       {
           steps.push_back(i * interval);
       }

       vtkSpoke *cornerSpokes[4];

       double  dXdu11[3], dXdv11[3],
               dXdu12[3], dXdv12[3],
               dXdu21[3], dXdv21[3],
               dXdu22[3], dXdv22[3];
       size_t snCols = static_cast<size_t>(nCols);
       // top row
       for(int i = 0; i < nCols-1; ++i)
       {
           ComputeDerivative(skeletalPts, 0, i, nRows, nCols, dXdu21, dXdv21);
           ComputeDerivative(skeletalPts, 0, i+1, nRows, nCols, dXdu22, dXdv22);
           dXdu11[0] = dXdu21[0];
           dXdu11[0] = dXdu21[0];
           dXdu11[1] = dXdu21[1];
           dXdv11[2] = dXdv21[2];
           dXdv11[1] = dXdv21[1];
           dXdv11[2] = dXdv21[2];

           dXdu12[0] = dXdu22[0];
           dXdu12[1] = dXdu22[1];
           dXdu12[2] = dXdu22[2];
           dXdv12[0] = dXdv22[0];
           dXdv12[1] = dXdv22[1];
           dXdv12[2] = dXdv22[2];
           interpolater.SetCornerDxdu(dXdu11,
                                      dXdu21,
                                      dXdu22,
                                      dXdu12);
           interpolater.SetCornerDxdv(dXdv11,
                                      dXdv21,
                                      dXdv22,
                                      dXdv12);
           size_t sti = static_cast<size_t>(i);
           cornerSpokes[0] = crestSpoke[sti];
           cornerSpokes[1] = interiorSpokes[sti];
           cornerSpokes[2] = interiorSpokes[sti+1];
           cornerSpokes[3] = crestSpoke[sti+1];
   //        for(size_t si = 0; si < steps.size(); ++si)
           {
               for(size_t sj = 0; sj < steps.size(); ++sj)
               {
                   vtkSpoke* in1 = new vtkSpoke;
                   interpolater.Interpolate(0.0, double(steps[sj]), cornerSpokes, in1);
                   crest.push_back(in1);

                   vtkSpoke* in2 = new vtkSpoke;
                   interpolater.Interpolate(1.0, double(steps[sj]), cornerSpokes, in2);
                   interior.push_back(in2);
               }
           }

       }
       // top right  edge
       cornerSpokes[0] = interiorSpokes[snCols-1];
       cornerSpokes[1] = interiorSpokes[snCols-1 + snCols];
       cornerSpokes[2] = crestSpoke[snCols+1];
       cornerSpokes[3] = crestSpoke[snCols-1];
       ComputeDerivative(skeletalPts, 0, nCols - 1, nRows, nCols, dXdu11, dXdv11);
       ComputeDerivative(skeletalPts, 1, nCols - 1, nRows, nCols, dXdu21, dXdv21);
       // revert dXdv
       dXdv11[0] *= -1;
       dXdv11[1] *= -1;
       dXdv11[2] *= -1;
       dXdv21[0] *= -1;
       dXdv21[1] *= -1;
       dXdv21[2] *= -1;
       dXdu12[0] = dXdu11[0];
       dXdu12[1] = dXdu11[1];
       dXdu12[2] = dXdu11[2];
       dXdv12[0] = dXdv11[0];
       dXdv12[1] = dXdv11[1];
       dXdv12[2] = dXdv11[2];

       dXdu22[0] = dXdu21[0];
       dXdu22[1] = dXdu21[1];
       dXdu22[2] = dXdu21[2];
       dXdv22[0] = dXdv21[0];
       dXdv22[1] = dXdv21[1];
       dXdv22[2] = dXdv21[2];
       interpolater.SetCornerDxdu(dXdu11,
                                  dXdu21,
                                  dXdu22,
                                  dXdu12);
       interpolater.SetCornerDxdv(dXdv11,
                                  dXdv21,
                                  dXdv22,
                                  dXdv12);

       for(size_t sj = 0; sj < steps.size(); ++sj)
       {
           vtkSpoke* in1 = new vtkSpoke;
           interpolater.Interpolate(double(steps[sj]), 0.0,  cornerSpokes, in1);
           interior.push_back(in1);

           vtkSpoke* in2 = new vtkSpoke;
           interpolater.Interpolate(double(steps[sj]), 1.0, cornerSpokes, in2);
           crest.push_back(in2);
       }

       // right col
       for(int i = nCols+1; i < nCols + 2 * (nRows - 2); i+=2)
       {
           size_t sti = static_cast<size_t>(i);
           int r = (i-nCols) / 2 + 1;
           int c = nCols - 1;
           size_t interiorId = static_cast<size_t>(nCols * r);

           if((i - nCols) % 2 == 0)
           {
               // left col
               continue;
           }
           else {
               // right col
               interiorId = static_cast<size_t>(nCols * (r+1) - 1);
               cornerSpokes[0] = interiorSpokes[interiorId];
               cornerSpokes[1] = interiorSpokes[interiorId + snCols];
               cornerSpokes[2] = crestSpoke[sti+2];
               cornerSpokes[3] = crestSpoke[sti];
               if(r == nRows - 2) {cornerSpokes[2] = crestSpoke[sti + snCols];}

               ComputeDerivative(skeletalPts, r, c, nRows, nCols, dXdu12, dXdv12);
               ComputeDerivative(skeletalPts, r+1, c, nRows, nCols, dXdu22, dXdv22);

               // revert dXdv
               dXdv12[0] *= -1;
               dXdv12[1] *= -1;
               dXdv12[2] *= -1;
               dXdv22[0] *= -1;
               dXdv22[1] *= -1;
               dXdv22[2] *= -1;
           }

           dXdu11[0] = dXdu12[0];
           dXdu11[1] = dXdu12[1];
           dXdu11[2] = dXdu12[2];
           dXdv11[0] = dXdv12[0];
           dXdv11[1] = dXdv12[1];
           dXdv11[2] = dXdv12[2];

           dXdu21[0] = dXdu22[0];
           dXdu21[1] = dXdu22[1];
           dXdu21[2] = dXdu22[2];
           dXdv21[0] = dXdv22[0];
           dXdv21[1] = dXdv22[1];
           dXdv21[2] = dXdv22[2];
           interpolater.SetCornerDxdu(dXdu11,
                                      dXdu21,
                                      dXdu22,
                                      dXdu12);
           interpolater.SetCornerDxdv(dXdv11,
                                      dXdv21,
                                      dXdv22,
                                      dXdv12);
           for(size_t sj = 0; sj < steps.size(); ++sj)
           {
               vtkSpoke* in1 = new vtkSpoke;
               interpolater.Interpolate(double(steps[sj]), 0.0,  cornerSpokes, in1);

               vtkSpoke* in2 = new vtkSpoke;
               interpolater.Interpolate(double(steps[sj]), 1.0, cornerSpokes, in2);
               crest.push_back(in2);
               interior.push_back(in1);
           }
       }

       // Bottom row from right to left
       for(int i = static_cast<int>(crestSpoke.size()-2); i >= nCols + 2 * (nRows - 2); --i)
       {
           ComputeDerivative(skeletalPts, nRows-1, i-(nCols + 2 * (nRows - 2)), nRows, nCols, dXdu11, dXdv11);
           ComputeDerivative(skeletalPts, nRows-1, i-(nCols + 2 * (nRows - 2))+1, nRows, nCols, dXdu12, dXdv12);
           dXdu21[0] = dXdu11[0];
           dXdu21[1] = dXdu11[1];
           dXdu21[2] = dXdu11[2];
           dXdv21[0] = dXdv11[0];
           dXdv21[1] = dXdv11[1];
           dXdv21[2] = dXdv11[2];

           dXdu22[0] = dXdu12[0];
           dXdu22[1] = dXdu12[1];
           dXdu22[2] = dXdu12[2];
           dXdv22[0] = dXdv12[0];
           dXdv22[1] = dXdv12[1];
           dXdv22[2] = dXdv12[2];
           interpolater.SetCornerDxdu(dXdu11,
                                      dXdu21,
                                      dXdu22,
                                      dXdu12);
           interpolater.SetCornerDxdv(dXdv11,
                                      dXdv21,
                                      dXdv22,
                                      dXdv12);
           size_t sti = static_cast<size_t>(i);
           size_t c = static_cast<size_t>(i - nCols - 2 * (nRows - 2));
           size_t interiorId = static_cast<size_t>((nRows - 1) * nCols) + c;
           cornerSpokes[0] = interiorSpokes[interiorId];
           cornerSpokes[1] = crestSpoke[sti];
           cornerSpokes[2] = crestSpoke[sti+1];
           cornerSpokes[3] = interiorSpokes[interiorId+1];

           size_t beginInteriorIndex = interior.size();
           size_t beginCrestIndex = crest.size();
           for(size_t sj = 0; sj < steps.size(); ++sj)
           {
               vtkSpoke* in1 = new vtkSpoke;
               interpolater.Interpolate(0.0, double(steps[sj]), cornerSpokes, in1);
               interior.push_back(in1);

               vtkSpoke* in2 = new vtkSpoke;
               interpolater.Interpolate(1.0, double(steps[sj]), cornerSpokes, in2);
               crest.push_back(in2);
           }
           std::reverse(std::begin(interior) + beginInteriorIndex, std::end(interior));
           std::reverse(std::begin(crest) + beginCrestIndex, std::end(crest));
       }


       // left col from down up
       for(int i = nCols + 2 * (nRows - 2) - 2; i >= nCols; i -= 2)
       {
           size_t sti = static_cast<size_t>(i);
           int r = (i-nCols) / 2 + 1;
           int c = 0;
           size_t interiorId = static_cast<size_t>(nCols * r);

           if((i - nCols) % 2 == 0)
           {
               // left col
               c = 0;
               cornerSpokes[0] = crestSpoke[sti];
               cornerSpokes[1] = crestSpoke[sti+2];
               cornerSpokes[2] = interiorSpokes[interiorId + snCols];
               cornerSpokes[3] = interiorSpokes[interiorId];
               ComputeDerivative(skeletalPts, r, c, nRows, nCols, dXdu12, dXdv12);
               ComputeDerivative(skeletalPts, r+1, c, nRows, nCols, dXdu22, dXdv22);
           }
           else {
               // right col
               continue;
           }

           dXdu11[0] = dXdu12[0];
           dXdu11[1] = dXdu12[1];
           dXdu11[2] = dXdu12[2];
           dXdv11[0] = dXdv12[0];
           dXdv11[1] = dXdv12[1];
           dXdv11[2] = dXdv12[2];

           dXdu21[0] = dXdu22[0];
           dXdu21[1] = dXdu22[1];
           dXdu21[2] = dXdu22[2];
           dXdv21[0] = dXdv22[0];
           dXdv21[1] = dXdv22[1];
           dXdv21[2] = dXdv22[2];
           interpolater.SetCornerDxdu(dXdu11,
                                      dXdu21,
                                      dXdu22,
                                      dXdu12);
           interpolater.SetCornerDxdv(dXdv11,
                                      dXdv21,
                                      dXdv22,
                                      dXdv12);
           size_t beginInteriorIndex = interior.size();
           size_t beginCrestIndex = crest.size();
           for(size_t sj = 0; sj < steps.size(); ++sj)
           {
               vtkSpoke* in1 = new vtkSpoke;
               interpolater.Interpolate(double(steps[sj]), 0.0,  cornerSpokes, in1);

               vtkSpoke* in2 = new vtkSpoke;
               interpolater.Interpolate(double(steps[sj]), 1.0, cornerSpokes, in2);
               crest.push_back(in1);
               interior.push_back(in2);
           }
           std::reverse(std::begin(interior) + beginInteriorIndex, std::end(interior));
           std::reverse(std::begin(crest) + beginCrestIndex, std::end(crest));
           if(i == nCols)
           {
               cornerSpokes[0] = crestSpoke[0];
               cornerSpokes[1] = crestSpoke[sti];
               cornerSpokes[2] = interiorSpokes[interiorId];
               cornerSpokes[3] = interiorSpokes[0];
               ComputeDerivative(skeletalPts, 0, 0, nRows, nCols, dXdu12, dXdv12);
               ComputeDerivative(skeletalPts, 1, 0, nRows, nCols, dXdu22, dXdv22);
               dXdu11[0] = dXdu12[0];
               dXdu11[1] = dXdu12[1];
               dXdu11[2] = dXdu12[2];
               dXdv11[0] = dXdv12[0];
               dXdv11[1] = dXdv12[1];
               dXdv11[2] = dXdv12[2];

               dXdu21[0] = dXdu22[0];
               dXdu21[1] = dXdu22[1];
               dXdu21[2] = dXdu22[2];
               dXdv21[0] = dXdv22[0];
               dXdv21[1] = dXdv22[1];
               dXdv21[2] = dXdv22[2];
               interpolater.SetCornerDxdu(dXdu11,
                                          dXdu21,
                                          dXdu22,
                                          dXdu12);
               interpolater.SetCornerDxdv(dXdv11,
                                          dXdv21,
                                          dXdv22,
                                          dXdv12);
               beginInteriorIndex = interior.size();
               beginCrestIndex = crest.size();
               for(size_t sj = 0; sj < steps.size(); ++sj)
               {
                   vtkSpoke* in1 = new vtkSpoke;
                   interpolater.Interpolate(double(steps[sj]), 0.0,  cornerSpokes, in1);
                   crest.push_back(in1);

                   vtkSpoke* in2 = new vtkSpoke;
                   interpolater.Interpolate(double(steps[sj]), 1.0, cornerSpokes, in2);
                   interior.push_back(in2);
               }
               std::reverse(std::begin(interior) + beginInteriorIndex, std::end(interior));
               std::reverse(std::begin(crest) + beginCrestIndex, std::end(crest));
           }
       }

}

void vtkSlicerSkeletalRepresentationRefinerLogic::ReorderCrestSpokes(int nRows, int nCols,
                                                                     std::vector<vtkSpoke *> &input,
                                                                     std::vector<vtkSpoke *> &output)
{
    for(int i = 0; i< nCols; ++i) {
        output.push_back(input[i]);
    }
    int nSideSpokes = (input.size() - 2 * nCols) / 2;
    for(int i = nCols; i < nSideSpokes + nCols; ++i) {
        output.push_back(input[input.size() - (i - nCols + 1)]); // left
        output.push_back(input[i]); // right

    }
    size_t beginCrestIndex = output.size();
    for(int i = nCols + nSideSpokes; i < input.size() - nSideSpokes; ++i) {
        output.push_back(input[i]);
    }
    std::reverse(std::begin(output) + beginCrestIndex, std::end(output));
}

void vtkSlicerSkeletalRepresentationRefinerLogic::OptimizeCrestSpokeLength(vtkImplicitPolyDataDistance *distanceFunction,
                                                                           vtkSpoke *targetSpoke, double stepSize, int maxIter)
{
    // 1. Transform the boundary point to image cs. by applying [x, y, z, 1] * mTransformationMat
    double pt[3];
    targetSpoke->GetBoundaryPoint(pt);
    double epsilon = 1e-5;

    double dist = distanceFunction->FunctionValue(pt);
    double oldR = targetSpoke->GetRadius();
    double newR = oldR;
    int iter = 0;
    double oldDist = dist;
    // 2. iteratively update
    while(abs(dist) > epsilon)
    {
        double newBdry[3];
        if(dist > 0)
        {
            // if the spoke is too long, shorten it
            newR -= stepSize;
            targetSpoke->SetRadius(newR);
            targetSpoke->GetBoundaryPoint(newBdry);
            dist = distanceFunction->FunctionValue(newBdry);
            if(oldDist * dist < 0) {
                // if the spoke change from outside to inside, decay the learning rate
                stepSize /= 10;
                oldDist = dist;
            }
        }
        else {
            // enlongate the spoke
            newR += stepSize;
            targetSpoke->SetRadius(newR);
            targetSpoke->GetBoundaryPoint(newBdry);
            dist = distanceFunction->FunctionValue(newBdry);
            // if the spoke change from outside to inside, decay the learning rate
            if(oldDist * dist < 0) {
                stepSize /= 10;
                oldDist = dist;
            }
        }

        iter++;
        if(iter > maxIter)
        {
            break;
        }
    }
}

void vtkSlicerSkeletalRepresentationRefinerLogic::Transform2ImageCS(double *ptInput, int *ptOutput)
{
    ptInput[0] = ptInput[0] * mTransformationMat[0][0] + mTransformationMat[3][0];
    ptInput[1] = ptInput[1] * mTransformationMat[1][1] + mTransformationMat[3][1];
    ptInput[2] = ptInput[2] * mTransformationMat[2][2] + mTransformationMat[3][2];

    ptInput[0] /= voxelSpacing;
    ptInput[1] /= voxelSpacing;
    ptInput[2] /= voxelSpacing;

    int x = static_cast<int>(ptInput[0]+0.5);
    int y = static_cast<int>(ptInput[1]+0.5);
    int z = static_cast<int>(ptInput[2]+0.5);

    int maxX = static_cast<int>(1 / voxelSpacing - 1);
    int maxY = static_cast<int>(1 / voxelSpacing - 1);
    int maxZ = static_cast<int>(1 / voxelSpacing - 1);

    if(x > maxX) x = maxX;
    if(y > maxY) y = maxY;
    if(z > maxZ) z = maxZ;

    if(x < 0) x = 0;
    if(y < 0) y = 0;
    if(z < 0) z = 0;

    ptOutput[0] = x;
    ptOutput[1] = y;
    ptOutput[2] = z;
}

void vtkSlicerSkeletalRepresentationRefinerLogic::ConvertPointCloud2Mesh(vtkPolyData *polyData)
{
    double bounds[6];
      polyData->GetBounds(bounds);
      double range[3];
      for (int i = 0; i < 3; ++i)
      {
        range[i] = bounds[2*i + 1] - bounds[2*i];
      }

      int sampleSize = polyData->GetNumberOfPoints() * .00005;
      if (sampleSize < 10)
      {
        sampleSize = 10;
      }
      std::cout << "Sample size is: " << sampleSize << " the number of points: " << polyData->GetNumberOfPoints() << std::endl;
      // Do we need to estimate normals?
      vtkSmartPointer<vtkSignedDistance> distance =
        vtkSmartPointer<vtkSignedDistance>::New();
      if (polyData->GetPointData()->GetNormals())
      {
        std::cout << "Using normals from input file" << std::endl;
        distance->SetInputData (polyData);
      }
      else
      {
        std::cout << "Estimating normals using PCANormalEstimation" << std::endl;
        vtkSmartPointer<vtkPCANormalEstimation> normals =
          vtkSmartPointer<vtkPCANormalEstimation>::New();
        normals->SetInputData (polyData);
        normals->SetSampleSize(sampleSize);
        normals->SetNormalOrientationToGraphTraversal();
        normals->FlipNormalsOn();
        distance->SetInputConnection (normals->GetOutputPort());
      }
      std::cout << "Range: "
                << range[0] << ", "
                << range[1] << ", "
                << range[2] << std::endl;
      int dimension = 256;
      double radius;
      radius = std::max(std::max(range[0], range[1]), range[2])
        / static_cast<double>(dimension) * 4; // ~4 voxels
      std::cout << "Radius: " << radius << std::endl;

      distance->SetRadius(radius);
      distance->SetDimensions(dimension, dimension, dimension);
      distance->SetBounds(
        bounds[0] - range[0] * .1,
        bounds[1] + range[0] * .1,
        bounds[2] - range[1] * .1,
        bounds[3] + range[1] * .1,
        bounds[4] - range[2] * .1,
        bounds[5] + range[2] * .1);

      vtkSmartPointer<vtkExtractSurface> surface =
        vtkSmartPointer<vtkExtractSurface>::New();
      surface->SetInputConnection (distance->GetOutputPort());
      surface->SetRadius(radius * .99);
      surface->Update();
      Visualize(surface->GetOutput(), "implied boundary", 0, 1, 1);
}

double vtkSlicerSkeletalRepresentationRefinerLogic::CLIDistance(int interpolationLevel,
                                                                const string &srepFileName,
                                                                const string &modelPrefix, const string &meshFileName)
{
    std::cout << "cli distance: img-file-name:" << meshFileName << " srep file name:" << srepFileName << std::endl;
    vtkSmartPointer<vtkPolyDataReader> reader = vtkSmartPointer<vtkPolyDataReader>::New();
    reader->SetFileName(meshFileName.c_str());
    reader->Update();
    vtkSmartPointer<vtkPolyData> inputMesh = reader->GetOutput();
    // 1. Parse the model into a parameter array that needs to be optimized
    int nRows = 0, nCols = 0;
    std::string up, down, crest;
    double crestShift = 0.0;
    ParseHeader(srepFileName, &nRows, &nCols, &crestShift, &up, &down, &crest);
    if(nRows == 0 || nCols == 0)
    {
        std::cerr << "The s-rep model is empty." << std::endl;
        return -1;
    }
    std::vector<double> up_coeff, up_radii, down_radii, up_dirs, down_dirs, up_skeletalPoints, down_skeletalPoints;
    Parse(up, up_coeff, up_radii, up_dirs, up_skeletalPoints);

    std::vector<vtkSpoke*> interpUpSpokes, interpDownSpokes;
    InterpolateSrep(interpolationLevel, nRows, nCols, up, crest, interpUpSpokes);
    InterpolateSrep(interpolationLevel, nRows, nCols, down, crest, interpDownSpokes);
    vtkSmartPointer<vtkCellLocator> cellLocator = vtkSmartPointer<vtkCellLocator>::New();
    cellLocator->SetDataSet(inputMesh);
    cellLocator->BuildLocator();
    double totalDist = 0.0;
    for(int i = 0; i < interpUpSpokes.size(); ++i){
        double pt[3], closestPt[3];
        interpUpSpokes[i]->GetBoundaryPoint(pt);
        vtkIdType cellId;
        int subId;
        double d; // unsigned distances
        cellLocator->FindClosestPoint(pt, closestPt, cellId, subId, d);
        totalDist += d;
    }
    for(int i = 0; i < interpDownSpokes.size(); ++i) {
        double pt[3], closestPt[3];
        interpDownSpokes[i]->GetBoundaryPoint(pt);
        vtkIdType cellId;
        int subId;
        double d; // unsigned distances
        cellLocator->FindClosestPoint(pt, closestPt, cellId, subId, d);
        totalDist += d;
    }
    double avgDist = totalDist / (double)(interpUpSpokes.size() + interpDownSpokes.size());
    return avgDist;
}

