#include "AnalysisAdaptor.h"
#include "ImageProperties.h"

#include <sensei/DataAdaptor.h>
#include <timer/Timer.h>

#include <vtkCellData.h>
#include <vtkDataArray.h>
#include <vtkDataObject.h>
#include <vtkImageData.h>
#include <vtkObjectFactory.h>
#include <vtkPointData.h>
#include <vtkPolyData.h>
#include <vtkRectilinearGrid.h>
#include <vtkStructuredGrid.h>
#include <vtkUnstructuredGrid.h>
#include <vtkCompositeDataSet.h>
#include <vtkCompositeDataIterator.h>

#include <VisItControlInterface_V2.h>
#include <VisItDataInterface_V2.h>

#include <sstream>
#include <algorithm>

#define DEBUG_PRINT

namespace sensei
{
namespace libsim
{

///////////////////////////////////////////////////////////////////////////////

class PlotRecord
{
public:
    PlotRecord() : imageProps(), plots(), plotVars(), slice(false), project2d(false)
    {
        origin[0] = origin[1] = origin[2] = 0.;
        normal[0] = 1.; normal[1] = normal[2] = 0.;
    }
    ~PlotRecord()
    {
    }

    static std::vector<std::string> SplitAtCommas(const std::string &s)
    {   
        std::stringstream ss(s);
        std::vector<std::string> result;
        while(ss.good())
        {
           std::string substr;
           getline(ss, substr, ',' );
           result.push_back(substr);
        }
        return result;
    }

    ImageProperties imageProps;
    std::vector<std::string> plots;
    std::vector<std::string> plotVars;
    bool slice;
    bool project2d;
    double origin[3];
    double normal[3];
};

class AnalysisAdaptor::PrivateData
{
public:
    PrivateData();
    ~PrivateData();

    void SetTraceFile(const std::string &s);
    void SetOptions(const std::string &s);
    void SetVisItDirectory(const std::string &s);
    void SetComm(MPI_Comm comm);

    void PrintSelf(ostream& os, vtkIndent indent);
    bool Initialize();
    bool Execute(DataAdaptor *dataAdaptor);

    bool AddPlots(const std::string &plots,
                  const std::string &plotVars,
                  bool slice, bool project2d,
                  const double origin[3], const double normal[3],
	          const ImageProperties &imgProps);
private:
    static int broadcast_int(int *value, int sender, void *cbdata);
    static int broadcast_string(char *str, int len, int sender, void *cbdata);
    static void SlaveProcessCallback(void *cbdata);
    static int          ActivateTimestep(void *cbdata);
    static visit_handle GetMetaData(void *cbdata);
    static visit_handle GetMesh(int dom, const char *name, void *cbdata);
    static visit_handle GetVariable(int dom, const char *name, void *cbdata);
    static visit_handle GetDomainList(const char *name, void *cbdata);

    int GetTotalDomains() const;
    int GetLocalDomain(int globaldomain) const;
    int TopologicalDimension(const int dims[3]) const;
    std::string MakeFileName(const std::string &f, int timestep, double time) const;

    DataAdaptor              *da;
    int                      *doms_per_rank;
    std::vector<vtkDataSet *> domains;
    std::string               traceFile, options, visitdir;
    std::vector<PlotRecord>   plots;
    MPI_Comm                  comm;
    static bool               runtimeLoaded;
    static int                instances;
};

bool AnalysisAdaptor::PrivateData::runtimeLoaded = false;
int  AnalysisAdaptor::PrivateData::instances = 0;

AnalysisAdaptor::PrivateData::PrivateData() : da(NULL), doms_per_rank(NULL),
    domains(), traceFile(), options(), visitdir()
{
    comm = MPI_COMM_WORLD;
    ++instances;
}

AnalysisAdaptor::PrivateData::~PrivateData()
{
    if(doms_per_rank != NULL)
    {
        delete [] doms_per_rank;
        doms_per_rank = NULL;
    }

    --instances;
    if(instances == 0 && runtimeLoaded && VisItIsConnected())
    {
        timer::MarkEvent mark("libsim::finalize");
        VisItDisconnect();
    }
}

void
AnalysisAdaptor::PrivateData::SetTraceFile(const std::string &s)
{
    traceFile = s;
}

void
AnalysisAdaptor::PrivateData::SetOptions(const std::string &s)
{
    options = s;
}

void
AnalysisAdaptor::PrivateData::SetVisItDirectory(const std::string &s)
{
    visitdir = s;
}

void
AnalysisAdaptor::PrivateData::SetComm(MPI_Comm c)
{
    comm = c;
}

void
AnalysisAdaptor::PrivateData::PrintSelf(ostream &os, vtkIndent indent)
{
    int rank = 0, size = 1;
    MPI_Comm_rank(comm, &rank);
    MPI_Comm_rank(comm, &size);
    if(rank == 0)
    {
        os << "traceFile = " << traceFile << endl;
        os << "options = " << options << endl;
        os << "visitdir = " << visitdir << endl;
        os << "runtimeLoaded = " << (runtimeLoaded ? "true" : "false") << endl;
#if 1
        cout << "doms_per_rank = {";
        for(int i = 0; i < size; ++i)
            cout << doms_per_rank[i] << ", ";
        cout << "}" << endl;
#endif
    }
}

bool
AnalysisAdaptor::PrivateData::AddPlots(const std::string &plts,
    const std::string &plotVars,
    bool slice, bool project2d,
    const double origin[3], const double normal[3],
    const ImageProperties &imgProps)
{
    PlotRecord p;
    p.imageProps = imgProps;
    p.plots = PlotRecord::SplitAtCommas(plts);
    p.plotVars = PlotRecord::SplitAtCommas(plotVars);
    p.slice = slice;
    p.project2d = project2d;
    memcpy(p.origin, origin, 3 * sizeof(double));
    memcpy(p.normal, normal, 3 * sizeof(double));

    bool retval = !p.plots.empty() && (p.plots.size() == p.plotVars.size());
    if(retval)
        plots.push_back(p);
    return retval;
}

bool
AnalysisAdaptor::PrivateData::Initialize()
{
    // Load the runtime if we have not done it before.
    if(!runtimeLoaded)
    {
        timer::MarkEvent mark("libsim::initialize");

        int rank = 0, size = 1;
        MPI_Comm_rank(comm, &rank);
        MPI_Comm_size(comm, &size);

        if(!traceFile.empty())
        {
            char suffix[100];
            snprintf(suffix, 100, ".%04d", rank);
            VisItOpenTraceFile((traceFile + suffix).c_str());
        }

        VisItDebug5("==== AnalysisAdaptor::PrivateData::Initialize ====\n");

        if(!options.empty())
            VisItSetOptions(const_cast<char*>(options.c_str()));

        if(!visitdir.empty())
            VisItSetDirectory(const_cast<char *>(visitdir.c_str()));

        // Install callback functions for global communication.
        VisItSetBroadcastIntFunction2(broadcast_int, this);
        VisItSetBroadcastStringFunction2(broadcast_string, this);

        // Tell libsim whether the simulation is parallel.
        VisItSetParallel(size > 1);
        VisItSetParallelRank(rank);

        // Install comm into VisIt.
        VisItSetMPICommunicator((void *)&comm);

        // Set up the environment.
        char *env = NULL;
        if(rank == 0)
            env = VisItGetEnvironment();
        VisItSetupEnvironment2(env);
        if(env != NULL)
            free(env);

        // Try and initialize the runtime.
        if(VisItInitializeRuntime() == VISIT_ERROR)
        {
            cerr << "Could not initialize the VisIt runtime library." << endl;
        }
        else
        {
            // Register Libsim callbacks.
            VisItSetSlaveProcessCallback2(SlaveProcessCallback, (void*)this); // needed in batch?
            //VisItSetActivateTimestep(ActivateTimestep, (void*)this); // Disable this b/c VisIt wasn't calling it anyway.
            VisItSetGetMetaData(GetMetaData, (void*)this);
            VisItSetGetMesh(GetMesh, (void*)this);
            VisItSetGetVariable(GetVariable, (void*)this);
            VisItSetGetDomainList(GetDomainList, (void*)this);

            runtimeLoaded = true;
        }
    }

    return runtimeLoaded;
}

std::string
AnalysisAdaptor::PrivateData::MakeFileName(const std::string &f, int timestep, double time) const
{
    std::string filename(f);

    char ts5[20];
    sprintf(ts5, "%05d", timestep);

    // replace "%ts" with timestep in filename
    std::string::size_type pos = filename.find("%ts");
    while (pos != std::string::npos)
    {
        filename.replace(pos, 3, ts5);
        pos = filename.find("%ts");
    }
    // replace "%t" with time in filename
    std::ostringstream t_stream;
    t_stream << time;
    pos = filename.find("%t");
    while (pos != std::string::npos)
    {
        filename.replace(pos, 2, t_stream.str());
        pos = filename.find("%t");
    }
    return filename;
}

bool
AnalysisAdaptor::PrivateData::Execute(DataAdaptor *dataAdaptor)
{
    VisItDebug5("==== AnalysisAdaptor::PrivateData::Execute ====\n");

    // Keep a pointer to the data adaptor so the callbacks can access it.
    da = dataAdaptor;

    // If we for some reason have not initialized by now, do it.
    int rank = 0;
    MPI_Comm_rank(comm, &rank);
    bool retval = Initialize();

#if 1
    // Let's get new metadata.
    VisItTimeStepChanged();

    // Now that the runtime stuff is loaded, we can execute some plots.
    for(size_t i = 0; i < plots.size(); ++i)
    {
        // Add all the plots in this group.
        int *ap = new int[plots[i].plots.size()];
        int np = 0;
        for(size_t j = 0; j < plots[i].plots.size(); ++j)
        {
           if(VisItAddPlot(plots[i].plots[j].c_str(),plots[i].plotVars[j].c_str()) == VISIT_OKAY)
           {
              // Use a better color table.
              const char *ctName = "hot_desaturated";
              if(plots[i].plots[j] == "Pseudocolor")
                 VisItSetPlotOptionsS("colorTableName", ctName);
              else if(plots[i].plots[j] == "Vector")
              {
                 VisItSetPlotOptionsS("colorTableName", ctName);
                 VisItSetPlotOptionsB("colorByMag", true);
              }

              ap[np] = np++;
           }
           else if(rank == 0)
              printf("ERROR: VisItAddPlot failed.\n");
        }

        // Select all plots.
        VisItSetActivePlots(ap, np);
        delete [] ap;

        // Add a slice operator to all plots.
        if(plots[i].slice)
        {
            VisItAddOperator("Slice", 1);
            VisItSetOperatorOptionsI("originType", 0); // point intercept
            VisItSetOperatorOptionsDv("originPoint", plots[i].origin, 3);
            VisItSetOperatorOptionsDv("normal", plots[i].normal, 3);
            VisItSetOperatorOptionsB("project2d", plots[i].project2d ? 1 : 0);
        }

        if(VisItDrawPlots() == VISIT_OKAY)
        {
            // Get the image properties.
            int w = plots[i].imageProps.GetWidth();
            int h = plots[i].imageProps.GetHeight();
            std::string filename;
            filename = MakeFileName(plots[i].imageProps.GetFilename(),
                                    dataAdaptor->GetDataTimeStep(),
                                    dataAdaptor->GetDataTime());
            int format = VISIT_IMAGEFORMAT_PNG;
            if(plots[i].imageProps.GetFormat() == "bmp")
                format = VISIT_IMAGEFORMAT_BMP;
            else if(plots[i].imageProps.GetFormat() == "jpeg")
                format = VISIT_IMAGEFORMAT_JPEG;
            else if(plots[i].imageProps.GetFormat() == "png")
                format = VISIT_IMAGEFORMAT_PNG;
            else if(plots[i].imageProps.GetFormat() == "ppm")
                format = VISIT_IMAGEFORMAT_PPM;
            else if(plots[i].imageProps.GetFormat() == "tiff")
                format = VISIT_IMAGEFORMAT_TIFF;

            // Save an image.
            if(VisItSaveWindow(filename.c_str(), w, h, format) == VISIT_OKAY)
            {
                retval = true;
            }
            else if(rank == 0)
                printf("ERROR: VisItSaveWindow failed.\n");
        }
        else if(rank == 0)
            printf("ERROR: VisItDrawPlots failed.\n");

        // Delete the plots.
        VisItDeleteActivePlots();
    }
#endif

    return retval;
}

int
AnalysisAdaptor::PrivateData::broadcast_int(int *value, int sender, void *cbdata)
{
    PrivateData *This = (PrivateData *)cbdata;
    return MPI_Bcast(value, 1, MPI_INT, sender, This->comm);
}

int
AnalysisAdaptor::PrivateData::broadcast_string(char *str, int len, int sender, void *cbdata)
{
    PrivateData *This = (PrivateData *)cbdata;
    return MPI_Bcast(str, len, MPI_CHAR, sender, This->comm);
}

void
AnalysisAdaptor::PrivateData::SlaveProcessCallback(void *cbdata)
{
    int value = 0;
    broadcast_int(&value, 0, cbdata);
}

int
AnalysisAdaptor::PrivateData::ActivateTimestep(void *cbdata)
{
    PrivateData *This = (PrivateData *)cbdata;
    DataAdaptor *da = This->da;
    VisItDebug5("==== AnalysisAdaptor::PrivateData::ActivateTimestep ====\n");

    // Clear the domains list. This is a local list for this rank.
    This->domains.clear();
    if(This->doms_per_rank != NULL)
    {
        delete [] This->doms_per_rank;
        This->doms_per_rank = NULL;
    }

    // Look at the data provided by the data adaptor. Determine the number
    // of domains that are being provided by the adaptor.
    vtkDataObject *obj = da->GetCompleteMesh();
    vtkCompositeDataSet *cds = vtkCompositeDataSet::SafeDownCast(obj);
    if(cds != NULL)
    {
        vtkCompositeDataIterator *it = cds->NewIterator();
        it->SkipEmptyNodesOn();
        it->InitTraversal();
        while(!it->IsDoneWithTraversal())
        {
            vtkDataObject *obj2 = cds->GetDataSet(it);
            if(obj2 != NULL && vtkDataSet::SafeDownCast(obj2) != NULL)
                This->domains.push_back(vtkDataSet::SafeDownCast(obj2));
            it->GoToNextItem();
        }
    }
    else
    {
        if(vtkDataSet::SafeDownCast(obj) != NULL)
            This->domains.push_back(vtkDataSet::SafeDownCast(obj));
    }

    // We need to determine the number of domains on each rank so we can 
    // make the right metadata.
    int rank = 0, size = 1;
    MPI_Comm_rank(This->comm, &rank);
    MPI_Comm_size(This->comm, &size);
    if(This->doms_per_rank == NULL)
        This->doms_per_rank = new int[size];
    memset(This->doms_per_rank, 0, sizeof(int) * size);
    int ndoms = (int)This->domains.size();
#ifdef DEDBUG_PRINT
    char tmp[100];
    sprintf(tmp, "Rank %d has %d domains.\n", rank, ndoms);
    VisItDebug5(tmp);
#endif

    MPI_Allgather(&ndoms, 1, MPI_INT,
                  This->doms_per_rank, 1, MPI_INT, This->comm);

#ifdef DEDBUG_PRINT
    VisItDebug5("doms_per_rank = {");
    for(int i = 0; i < size; ++i)
    {
        sprintf(tmp, "%d, ", This->doms_per_rank[i]);
        VisItDebug5(tmp);
    }
    VisItDebug5("}\n");
#endif
}

int
AnalysisAdaptor::PrivateData::GetTotalDomains() const
{
    int size = 1;
    MPI_Comm_size(comm, &size);
    int total = 0;
    for(int i = 0; i < size; ++i)
        total += doms_per_rank[i];
    return total;
}

int
AnalysisAdaptor::PrivateData::GetLocalDomain(int globaldomain) const
{
    int rank = 0;
    MPI_Comm_rank(comm, &rank);
    int offset = 0;
    for(int i = 0; i < rank; ++i)
        offset += doms_per_rank[i];

    int dom = -1;
    if(globaldomain >= offset && globaldomain < offset+doms_per_rank[rank])
        dom = globaldomain - offset;

    return dom;
}

int
AnalysisAdaptor::PrivateData::TopologicalDimension(const int dims[3]) const
{
    int d = 0;
    if(dims[0] > 1) ++d;
    if(dims[1] > 1) ++d;
    if(dims[2] > 1) ++d;
    return d;
}

visit_handle
AnalysisAdaptor::PrivateData::GetMetaData(void *cbdata)
{
    PrivateData *This = (PrivateData *)cbdata;
    DataAdaptor *da = This->da;
    visit_handle md = VISIT_INVALID_HANDLE;

    // HACK: VisIt is not calling ActivateTimestep. Do it here.
    ActivateTimestep(cbdata);

    VisItDebug5("==== AnalysisAdaptor::PrivateData::GetMetaData ====\n");

    /* Create metadata. */
    if(VisIt_SimulationMetaData_alloc(&md) == VISIT_OKAY)
    {
        int i;
        visit_handle mmd = VISIT_INVALID_HANDLE;
        visit_handle vmd = VISIT_INVALID_HANDLE;

        /* Set the simulation state. */
        VisIt_SimulationMetaData_setMode(md, VISIT_SIMMODE_RUNNING);
        VisIt_SimulationMetaData_setCycleTime(md, da->GetDataTimeStep(), da->GetDataTime());

        /* Add mesh metadata. */
        if(VisIt_MeshMetaData_alloc(&mmd) == VISIT_OKAY)
        {
// ASSUMPTION FOR NOW: domains will not be empty and will be the same type data on all ranks.

            vtkDataSet *ds = This->domains[0];

            vtkImageData       *igrid = vtkImageData::SafeDownCast(ds);
            vtkRectilinearGrid *rgrid = vtkRectilinearGrid::SafeDownCast(ds);
            vtkStructuredGrid  *sgrid = vtkStructuredGrid::SafeDownCast(ds);
            vtkUnstructuredGrid*ugrid = vtkUnstructuredGrid::SafeDownCast(ds);
            vtkPolyData        *pgrid = vtkPolyData::SafeDownCast(ds);
            int dims[3];
            if(igrid != NULL)
            {
                igrid->GetDimensions(dims);
                VisIt_MeshMetaData_setMeshType(mmd, VISIT_MESHTYPE_RECTILINEAR);
                VisIt_MeshMetaData_setTopologicalDimension(mmd, This->TopologicalDimension(dims));
                VisIt_MeshMetaData_setSpatialDimension(mmd, This->TopologicalDimension(dims));
            }
            else if(rgrid != NULL)
            {
                rgrid->GetDimensions(dims);
                VisIt_MeshMetaData_setMeshType(mmd, VISIT_MESHTYPE_RECTILINEAR);
                VisIt_MeshMetaData_setTopologicalDimension(mmd, This->TopologicalDimension(dims));
                VisIt_MeshMetaData_setSpatialDimension(mmd, This->TopologicalDimension(dims));
            }
            else if(sgrid != NULL)
            {
                sgrid->GetDimensions(dims);
                VisIt_MeshMetaData_setMeshType(mmd, VISIT_MESHTYPE_CURVILINEAR);
                VisIt_MeshMetaData_setTopologicalDimension(mmd, This->TopologicalDimension(dims));
                VisIt_MeshMetaData_setSpatialDimension(mmd, This->TopologicalDimension(dims));
            }
            else if(ugrid != NULL)
            {
                VisIt_MeshMetaData_setMeshType(mmd, VISIT_MESHTYPE_UNSTRUCTURED);
                VisIt_MeshMetaData_setTopologicalDimension(mmd, 3); // just do 3.
                VisIt_MeshMetaData_setSpatialDimension(mmd, 3);
            }
            else if(pgrid != NULL)
            {
                VisIt_MeshMetaData_setMeshType(mmd, VISIT_MESHTYPE_UNSTRUCTURED);
                VisIt_MeshMetaData_setTopologicalDimension(mmd, 2);
                VisIt_MeshMetaData_setSpatialDimension(mmd, 3);
            }

            /* Set the mesh's properties.*/
            VisIt_MeshMetaData_setName(mmd, "mesh");
            VisIt_MeshMetaData_setNumDomains(mmd, This->GetTotalDomains());
            VisIt_SimulationMetaData_addMesh(md, mmd);
        }

        // Add variables.
        int assoc = vtkDataObject::FIELD_ASSOCIATION_POINTS;
        std::vector<std::string> nodal_vars;
        for(i = 0; i < da->GetNumberOfArrays(assoc); ++i)
        {
            if(VisIt_VariableMetaData_alloc(&vmd) == VISIT_OKAY)
            {
                VisIt_VariableMetaData_setName(vmd, da->GetArrayName(assoc, i).c_str());
                VisIt_VariableMetaData_setMeshName(vmd, "mesh");
                VisIt_VariableMetaData_setType(vmd, VISIT_VARTYPE_SCALAR);
                VisIt_VariableMetaData_setCentering(vmd, VISIT_VARCENTERING_NODE);
                VisIt_SimulationMetaData_addVariable(md, vmd);

                nodal_vars.push_back(da->GetArrayName(assoc, i).c_str());
            }
        }
        assoc = vtkDataObject::FIELD_ASSOCIATION_CELLS;
        for(i = 0; i < da->GetNumberOfArrays(assoc); ++i)
        {
            // See if the variable is already in the nodal vars. If so,
            // we prepend "cell_" to the name.
            std::string var(da->GetArrayName(assoc, i));
            bool alreadyDefined = std::find(nodal_vars.begin(), nodal_vars.end(), var) != nodal_vars.end();
            if(alreadyDefined)
                var = std::string("cell_") + var;

            if(VisIt_VariableMetaData_alloc(&vmd) == VISIT_OKAY)
            {
                VisIt_VariableMetaData_setName(vmd, var.c_str());
                VisIt_VariableMetaData_setMeshName(vmd, "mesh");
                VisIt_VariableMetaData_setType(vmd, VISIT_VARTYPE_SCALAR);
                VisIt_VariableMetaData_setCentering(vmd, VISIT_VARCENTERING_ZONE);
                VisIt_SimulationMetaData_addVariable(md, vmd);
            }
        }
    }

    return md;
}

visit_handle
AnalysisAdaptor::PrivateData::GetMesh(int dom, const char *name, void *cbdata)
{
    PrivateData *This = (PrivateData *)cbdata;
    DataAdaptor *da = This->da;
    int localdomain = This->GetLocalDomain(dom);
    visit_handle mesh = VISIT_INVALID_HANDLE;
    VisItDebug5("==== AnalysisAdaptor::PrivateData::GetMesh ====\n");
#ifdef DEBUG_PRINT
    char tmp[200];
    sprintf(tmp, "\tdom=%d, localdomain = %d, This->domains.size()=%d\n",
            dom, localdomain, (int)This->domains.size());
    VisItDebug5(tmp);
#endif

    if(localdomain >= 0 && localdomain < (int)This->domains.size())
    {
        vtkDataSet *ds = This->domains[localdomain];
        vtkImageData *igrid = vtkImageData::SafeDownCast(ds);
        if(igrid != NULL)
        {
            VisItDebug5("\tExposing vtkImageData as a rectilinear grid.\n");

            // We already have a VTK dataset. Libsim doesn't have a path to just
            // pass it through to SimV2+VisIt so we have to pull some details
            // out to make the right Libsim calls so the SimV2 reader will be
            // able to make the right VTK dataset on the other end. Silly/Stupid
            // but giving VTK datasets to Libsim has never come up before.

            int dims[3];
            igrid->GetDimensions(dims);
            int x0, x1, y0, y1, z0, z1;
            igrid->GetExtent(x0, x1, y0, y1, z0, z1);
#ifdef DEBUG_PRINT
            sprintf(tmp, "\tdims={%d,%d,%d}\n", dims[0], dims[1], dims[2]);
            VisItDebug5(tmp);
            sprintf(tmp, "\textents={%d,%d,%d,%d,%d,%d}\n", x0, x1, y0, y1, z0, z1);
            VisItDebug5(tmp);
#endif
            if(VisIt_RectilinearMesh_alloc(&mesh) == VISIT_OKAY)
            {
                int nx = std::max(dims[0], 1);
                int ny = std::max(dims[1], 1);
                int nz = std::max(dims[2], 1);
                float *x = (float *)malloc(sizeof(float) * nx);
                float *y = (float *)malloc(sizeof(float) * ny);
                float *z = (float *)malloc(sizeof(float) * nz);
                if(x != NULL && y != NULL && z != NULL)
                {
                    visit_handle xc, yc, zc;
                    if(VisIt_VariableData_alloc(&xc) == VISIT_OKAY &&
                       VisIt_VariableData_alloc(&yc) == VISIT_OKAY &&
                       VisIt_VariableData_alloc(&zc) == VISIT_OKAY)
                    {
                        for(int i = 0; i < nx; ++i)
                            x[i] = x0 + i;
                        for(int i = 0; i < ny; ++i)
                            y[i] = y0 + i;
                        for(int i = 0; i < nz; ++i)
                            z[i] = z0 + i;
                        VisIt_VariableData_setDataF(xc, VISIT_OWNER_VISIT, 1, nx, x);
                        VisIt_VariableData_setDataF(yc, VISIT_OWNER_VISIT, 1, ny, y);
                        VisIt_VariableData_setDataF(zc, VISIT_OWNER_VISIT, 1, nz, z);
                        VisIt_RectilinearMesh_setCoordsXYZ(mesh, xc, yc, zc);
                    }
                    else
                    {
                        VisIt_RectilinearMesh_free(mesh);
                        mesh = VISIT_INVALID_HANDLE;
                        if(xc != VISIT_INVALID_HANDLE)
                            VisIt_VariableData_free(xc);
                        if(yc != VISIT_INVALID_HANDLE)
                            VisIt_VariableData_free(yc);
                        if(zc != VISIT_INVALID_HANDLE)
                            VisIt_VariableData_free(zc);
                        if(x != NULL) free(x);
                        if(y != NULL) free(y);
                        if(z != NULL) free(z);
                    }
                }
                else
                {
                    VisIt_RectilinearMesh_free(mesh);
                    mesh = VISIT_INVALID_HANDLE;
                    if(x != NULL) free(x);
                    if(y != NULL) free(y);
                    if(z != NULL) free(z);
                }
            }
        }
        // TODO: expand to other mesh types.
        else
        {
            VisItDebug5("Unsupported VTK mesh type.\n");
        }
    }

    return mesh;
}

visit_handle
AnalysisAdaptor::PrivateData::GetVariable(int dom, const char *name, void *cbdata)
{
    PrivateData *This = (PrivateData *)cbdata;
    DataAdaptor *da = This->da;
    int localdomain = This->GetLocalDomain(dom);
    visit_handle h = VISIT_INVALID_HANDLE;
    VisItDebug5("==== AnalysisAdaptor::PrivateData::GetVariable ====\n");

    if(localdomain >= 0 && localdomain < (int)This->domains.size())
    {
        // Get the right data array from the VTK dataset.
        vtkDataSet *ds = This->domains[localdomain];
        vtkDataArray *arr = NULL;
        // First check the point data.
        for(int i = 0; i < ds->GetPointData()->GetNumberOfArrays(); ++i)
        {
            if(strcmp(name, ds->GetPointData()->GetArray(i)->GetName()) == 0)
            {
                arr = ds->GetPointData()->GetArray(i);
                VisItDebug5("==== Found point data ====\n");
                break;
            }
        }
        // Next, check the cell data. Note that we also check a variable 
        // called "cell_"+name in case we had to rename if there were
        // duplicate names.
        if(arr == NULL)
        {
            std::string namestr(name);
            for(int i = 0; i < ds->GetCellData()->GetNumberOfArrays(); ++i)
            {
                std::string arrname(ds->GetCellData()->GetArray(i)->GetName());
                std::string cellarrname(std::string("cell_") + arrname);
                if(namestr == arrname || namestr == cellarrname)
                {
                    arr = ds->GetCellData()->GetArray(i);
                    VisItDebug5("==== Found cell data ====\n");
                    break;
                }
            }
        }

        if(arr != NULL)
        {
            char tmp[100];
            // If we have a standard memory layout in a supported type, 
            // zero-copy expose the data to libsim.
            VisIt_VariableData_alloc(&h);
            bool copy = false;
            int nc = arr->GetNumberOfComponents();
            int nt = arr->GetNumberOfTuples();
            if(arr->HasStandardMemoryLayout())
            {
                if(arr->GetDataType() == VTK_CHAR)
                    VisIt_VariableData_setDataC(h, VISIT_OWNER_SIM, nc, nt, (char *)arr->GetVoidPointer(0));
                else if(arr->GetDataType() == VTK_INT)
                    VisIt_VariableData_setDataI(h, VISIT_OWNER_SIM, nc, nt, (int *)arr->GetVoidPointer(0));
                else if(arr->GetDataType() == VTK_LONG)
                    VisIt_VariableData_setDataL(h, VISIT_OWNER_SIM, nc, nt, (long *)arr->GetVoidPointer(0));
                else if(arr->GetDataType() == VTK_FLOAT)
                    VisIt_VariableData_setDataF(h, VISIT_OWNER_SIM, nc, nt, (float *)arr->GetVoidPointer(0));
                else if(arr->GetDataType() == VTK_DOUBLE)
                    VisIt_VariableData_setDataD(h, VISIT_OWNER_SIM, nc, nt, (double *)arr->GetVoidPointer(0));
                else
                    copy = true;

                if(!copy)
                {
                    sprintf(tmp, "==== Standard memory layout: nc=%d, nt=%d ====\n", nc, nt);
                    VisItDebug5(tmp);
                }
            }
            else
            {
                // NOTE: we could detect some non-contiguous memory layouts here and
                //       expose to Libsim that way. Just copy for now...
                copy = true;
            }

            // Expose the data as a copy, converting to double.
            if(copy)
            {
                sprintf(tmp, "==== Copying required: nc=%d, nt=%d ====\n", nc, nt);
                VisItDebug5(tmp);

                double *v = (double *)malloc(sizeof(double) * nc * nt);
                double *tuple = v;
                for(int i = 0; i < nt; ++i)
                {
                    arr->GetTuple(i, tuple);
                    tuple += nc;
                }
                VisIt_VariableData_setDataD(h, VISIT_OWNER_VISIT, nc, nt, v);
            }
        }
    }

    return h;
}

visit_handle
AnalysisAdaptor::PrivateData::GetDomainList(const char *name, void *cbdata)
{
    PrivateData *This = (PrivateData *)cbdata;
    visit_handle h = VISIT_INVALID_HANDLE;
    VisItDebug5("==== AnalysisAdaptor::PrivateData::GetDomainList ====\n");

    if(VisIt_DomainList_alloc(&h) != VISIT_ERROR)
    {
        visit_handle hdl;
        int i, *iptr = NULL;

        int rank = 0, size = 1;
        MPI_Comm_rank(This->comm, &rank);
        MPI_Comm_size(This->comm, &size);

        // Compute the offset to this rank's domains.
        int offset = 0;
        for(int i = 0; i < rank; ++i)
            offset += This->doms_per_rank[i];

        // Make a list of this rank's domains using global domain ids.
        iptr = (int *)malloc(sizeof(int) * This->doms_per_rank[rank]);
        for(i = 0; i < This->doms_per_rank[rank]; ++i)
            iptr[i] = offset + i;

        VisIt_VariableData_alloc(&hdl);
        VisIt_VariableData_setDataI(hdl, VISIT_OWNER_VISIT, 1, This->doms_per_rank[rank], iptr);
        VisIt_DomainList_setDomains(h, This->GetTotalDomains(), hdl);
    }
    return h;
}

///////////////////////////////////////////////////////////////////////////////
#if VTK_MAJOR_VERSION == 6 && VTK_MINOR_VERSION == 1
AnalysisAdaptor *AnalysisAdaptor::New() { return new AnalysisAdaptor; }
#else
vtkStandardNewMacro(AnalysisAdaptor);
#endif

//-----------------------------------------------------------------------------
AnalysisAdaptor::AnalysisAdaptor()
{
    d = new PrivateData;
}

//-----------------------------------------------------------------------------
AnalysisAdaptor::~AnalysisAdaptor()
{
    delete d;
}

//-----------------------------------------------------------------------------
void
AnalysisAdaptor::SetTraceFile(const std::string &s)
{
    d->SetTraceFile(s);
}

//-----------------------------------------------------------------------------
void
AnalysisAdaptor::SetOptions(const std::string &s)
{
    d->SetOptions(s);
}

//-----------------------------------------------------------------------------
void
AnalysisAdaptor::SetVisItDirectory(const std::string &s)
{
    d->SetVisItDirectory(s);
}

//-----------------------------------------------------------------------------
void
AnalysisAdaptor::SetComm(MPI_Comm c)
{
    d->SetComm(c);
}

//-----------------------------------------------------------------------------
bool
AnalysisAdaptor::AddPlots(const std::string &plots,
    const std::string &plotVars,
    bool slice, bool project2d,
    const double origin[3], const double normal[3],
    const ImageProperties &imgProps)
{
    return d->AddPlots(plots, plotVars, slice, project2d, origin, normal, imgProps);
}

//-----------------------------------------------------------------------------
void
AnalysisAdaptor::Initialize()
{
    d->Initialize();
}

//-----------------------------------------------------------------------------
bool AnalysisAdaptor::Execute(DataAdaptor* dataAdaptor)
{
    timer::MarkEvent mark("libsim::execute");
    return d->Execute(dataAdaptor);
}

//-----------------------------------------------------------------------------
void AnalysisAdaptor::PrintSelf(ostream& os, vtkIndent indent)
{
    this->Superclass::PrintSelf(os, indent);
    d->PrintSelf(os, indent);
}

} // libsim
} // sensei