/*
  Copyright 2012 SINTEF ICT, Applied Mathematics.

  This file is part of the Open Porous Media project (OPM).

  OPM is free software: you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published by
  the Free Software Foundation, either version 3 of the License, or
  (at your option) any later version.

  OPM is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
  GNU General Public License for more details.

  You should have received a copy of the GNU General Public License
  along with OPM.  If not, see <http://www.gnu.org/licenses/>.
*/

#include "config.h"

#include <opm/grid/GridHelpers.hpp>
#include <opm/grid/GridManager.hpp>
#include <opm/grid/UnstructuredGrid.h>
#include <opm/grid/cart_grid.h>
#include <opm/grid/cornerpoint_grid.h>
#include <opm/grid/MinpvProcessor.hpp>
#include <opm/grid/utility/ErrorMacros.hpp>

#include <opm/grid/utility/OpmParserIncludes.hpp>

#include <array>
#include <algorithm>
#include <numeric>

namespace Opm
{

#if HAVE_ECL_INPUT
    /// Construct a 3d corner-point grid from a deck.
    GridManager::GridManager(const Opm::EclipseGrid& inputGrid)
        : ug_(0)
    {
        initFromEclipseGrid(inputGrid, std::vector<double>());
    }


    GridManager::GridManager(const Opm::EclipseGrid& inputGrid,
                             const std::vector<double>& poreVolumes)
        : ug_(0)
    {
        initFromEclipseGrid(inputGrid, poreVolumes);
    }
#endif


    /// Construct a 2d cartesian grid with cells of unit size.
    GridManager::GridManager(int nx, int ny)
    {
        ug_ = create_grid_cart2d(nx, ny, 1.0, 1.0);
        if (!ug_) {
            OPM_THROW(std::runtime_error, "Failed to construct grid.");
        }
    }

    GridManager::GridManager(int nx, int ny,double dx, double dy)
    {
        ug_ = create_grid_cart2d(nx, ny, dx, dy);
        if (!ug_) {
            OPM_THROW(std::runtime_error, "Failed to construct grid.");
        }
    }


    /// Construct a 3d cartesian grid with cells of unit size.
    GridManager::GridManager(int nx, int ny, int nz)
    {
        ug_ = create_grid_cart3d(nx, ny, nz);
        if (!ug_) {
            OPM_THROW(std::runtime_error, "Failed to construct grid.");
        }
    }




    /// Construct a 3d cartesian grid with cells of size [dx, dy, dz].
    GridManager::GridManager(int nx, int ny, int nz,
                             double dx, double dy, double dz)
    {
        ug_ = create_grid_hexa3d(nx, ny, nz, dx, dy, dz);
        if (!ug_) {
            OPM_THROW(std::runtime_error, "Failed to construct grid.");
        }
    }




    /// Construct a grid from an input file.
    /// The file format used is currently undocumented,
    /// and is therefore only suited for internal use.
    GridManager::GridManager(const std::string& input_filename)
    {
        ug_ = read_grid(input_filename.c_str());
        if (!ug_) {
            OPM_THROW(std::runtime_error, "Failed to read grid from file " << input_filename);
        }
    }

    /// Destructor.
    GridManager::~GridManager()
    {
        destroy_grid(ug_);
    }




    /// Access the managed UnstructuredGrid.
    /// The method is named similarly to c_str() in std::string,
    /// to make it clear that we are returning a C-compatible struct.
    const UnstructuredGrid* GridManager::c_grid() const
    {
        return ug_;
    }



#if HAVE_ECL_INPUT
    // Construct corner-point grid from EclipseGrid.
    void GridManager::initFromEclipseGrid(const Opm::EclipseGrid& inputGrid,
                                          const std::vector<double>& poreVolumes)
    {
        struct grdecl g;
        int cells_modified = 0;
        std::vector<int> actnum;
        std::vector<double> coord;
        std::vector<double> zcorn;
        std::vector<double> mapaxes;

        g.dims[0] = inputGrid.getNX();
        g.dims[1] = inputGrid.getNY();
        g.dims[2] = inputGrid.getNZ();

        inputGrid.exportMAPAXES( mapaxes );
        inputGrid.exportCOORD( coord );
        inputGrid.exportZCORN( zcorn );
        inputGrid.exportACTNUM( actnum );

        g.coord = coord.data();
        g.zcorn = zcorn.data();
        g.actnum = actnum.data();
        g.mapaxes = mapaxes.data();

        if (!poreVolumes.empty() && (inputGrid.getMinpvMode() != MinpvMode::ModeEnum::Inactive)) {
            MinpvProcessor mp(g.dims[0], g.dims[1], g.dims[2]);
            const double minpv_value  = inputGrid.getMinpvValue();
            // Currently the pinchProcessor is not used and only opmfil is supported
            //bool opmfil = inputGrid.getMinpvMode() == MinpvMode::OpmFIL;
            bool opmfil = true;
            cells_modified = mp.process(poreVolumes, minpv_value, actnum, opmfil, zcorn.data());
        }

        const double z_tolerance = inputGrid.isPinchActive() ? inputGrid.getPinchThresholdThickness() : 0.0;
        ug_ = create_grid_cornerpoint(&g, z_tolerance);
        if (!ug_) {
            OPM_THROW(std::runtime_error, "Failed to construct grid.");
        }

        if (cells_modified > 0) {
            attach_zcorn_copy( ug_ , zcorn.data() );
        }
    }


    void GridManager::createGrdecl(const Opm::Deck& deck, struct grdecl &grdecl)
    {
        // Extract data from deck.
        const std::vector<double>& zcorn = deck.getKeyword("ZCORN").getSIDoubleData();
        const std::vector<double>& coord = deck.getKeyword("COORD").getSIDoubleData();
        const int* actnum = NULL;
        if (deck.hasKeyword("ACTNUM")) {
            actnum = &(deck.getKeyword("ACTNUM").getIntData()[0]);
        }

        std::array<int, 3> dims;
        if (deck.hasKeyword("DIMENS")) {
            const auto& dimensKeyword = deck.getKeyword("DIMENS");
            dims[0] = dimensKeyword.getRecord(0).getItem(0).get< int >(0);
            dims[1] = dimensKeyword.getRecord(0).getItem(1).get< int >(0);
            dims[2] = dimensKeyword.getRecord(0).getItem(2).get< int >(0);
        } else if (deck.hasKeyword("SPECGRID")) {
            const auto& specgridKeyword = deck.getKeyword("SPECGRID");
            dims[0] = specgridKeyword.getRecord(0).getItem(0).get< int >(0);
            dims[1] = specgridKeyword.getRecord(0).getItem(1).get< int >(0);
            dims[2] = specgridKeyword.getRecord(0).getItem(2).get< int >(0);
        } else {
            OPM_THROW(std::runtime_error, "Deck must have either DIMENS or SPECGRID.");
        }

        // Collect in input struct for preprocessing.

        grdecl.zcorn = &zcorn[0];
        grdecl.coord = &coord[0];
        grdecl.actnum = actnum;
        grdecl.dims[0] = dims[0];
        grdecl.dims[1] = dims[1];
        grdecl.dims[2] = dims[2];

        if (deck.hasKeyword("MAPAXES")) {
            const auto& mapaxesKeyword = deck.getKeyword("MAPAXES");
            const auto& mapaxesRecord = mapaxesKeyword.getRecord(0);

            // memleak alert: here we need to make sure that C code
            // can properly take ownership of the grdecl.mapaxes
            // object. if it is not freed, it will result in a
            // memleak...
            double *cWtfMapaxes = static_cast<double*>(malloc(sizeof(double)*mapaxesRecord.size()));
            for (unsigned i = 0; i < mapaxesRecord.size(); ++i)
                cWtfMapaxes[i] = mapaxesRecord.getItem(i).getSIDouble(0);
            grdecl.mapaxes = cWtfMapaxes;
        } else
            grdecl.mapaxes = NULL;

    }
#endif






} // namespace Opm
