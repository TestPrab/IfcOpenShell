/********************************************************************************
*                                                                              *
* This file is part of IfcOpenShell.                                           *
*                                                                              *
* IfcOpenShell is free software: you can redistribute it and/or modify         *
* it under the terms of the Lesser GNU General Public License as published by  *
* the Free Software Foundation, either version 3.0 of the License, or          *
* (at your option) any later version.                                          *
*                                                                              *
* IfcOpenShell is distributed in the hope that it will be useful,              *
* but WITHOUT ANY WARRANTY; without even the implied warranty of               *
* MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the                 *
* Lesser GNU General Public License for more details.                          *
*                                                                              *
* You should have received a copy of the Lesser GNU General Public License     *
* along with this program. If not, see <http://www.gnu.org/licenses/>.         *
*                                                                              *
********************************************************************************/

#ifndef IFCGEOMTREE_H
#define IFCGEOMTREE_H

#include "../ifcparse/IfcFile.h"
#include "../ifcgeom/IfcGeomElement.h"
#include "../ifcgeom_schema_agnostic/IfcGeomIterator.h"
#include "../ifcgeom_schema_agnostic/Kernel.h"

#include <NCollection_UBTree.hxx>
#include <BRepBndLib.hxx>
#include <Bnd_Box.hxx>
#include <BRep_Builder.hxx>
#include <BRepAlgoAPI_Common.hxx>
#include <BRepAlgoAPI_Cut.hxx>
#include <BRepExtrema_DistShapeShape.hxx>
#include <BRepClass3d_SolidClassifier.hxx>

namespace IfcGeom {

	namespace impl {
		template <typename T>
		class tree {

			bool test(const TopoDS_Shape& A, const TopoDS_Shape& B, bool completely_within, double extend) const {
				if (extend > 0.) {
					BRepExtrema_DistShapeShape dss(A, B);
					if (dss.Perform() && dss.NbSolution() >= 1) {
						return dss.Value() <= extend;
					}
				} else if (completely_within) {
					BRepAlgoAPI_Cut cut(B, A);
					if (cut.IsDone()) {
						if (IfcGeom::Kernel::count(cut.Shape(), TopAbs_SHELL) == 0) {
							return true;
						}
					}
				} else {
					BRepAlgoAPI_Common common(A, B);
					if (common.IsDone()) {
						if (IfcGeom::Kernel::count(common.Shape(), TopAbs_SHELL) > 0) {
							return true;
						}
					}
				}
				return false;
			}

		public:

			void add(const T& t, const Bnd_Box& b) {
				tree_.Add(t, b);
			}

			void add(const T& t, const TopoDS_Shape& s) {
				Bnd_Box b;
				BRepBndLib::AddClose(s, b);
				add(t, b);
				shapes_[t] = s;
			}

			std::vector<T> select_box(const T& t, bool completely_within = false, double extend=-1.e-5) const {
				typename map_t::const_iterator it = shapes_.find(t);
				if (it == shapes_.end()) {
					return std::vector<T>();
				}

				Bnd_Box b;
				BRepBndLib::AddClose(it->second, b);

				// Gap is assumed to be positive throughout the codebase,
				// but at least for IsOut() in the selector a negative
				// Gap should work as well.
				b.SetGap(b.GetGap() + extend); 
				
				return select_box(b, completely_within);
			}

			std::vector<T> select_box(const gp_Pnt& p, double extend=0.0) const {
				Bnd_Box b;
				b.Add(p);
				b.SetGap(b.GetGap() + extend);
				return select_box(b);
			}

			std::vector<T> select_box(const Bnd_Box& b, bool completely_within = false) const {
				selector s(b);
				tree_.Select(s);
				if (completely_within) {
					std::vector<T> ts = s.results();
					std::vector<T> ts_filtered;
					ts_filtered.reserve(ts.size());
					typename std::vector<T>::const_iterator it = ts.begin();
					for (; it != ts.end(); ++it) {
						const TopoDS_Shape& shp = shapes_.find(*it)->second;
						Bnd_Box B;
						BRepBndLib::AddClose(shp, B);

						// BndBox::CornerMin() /-Max() introduced in OCCT 6.8
						double x1, y1, z1, x2, y2, z2;
						b.Get(x1, y1, z1, x2, y2, z2);
						double gap = B.GetGap();
						gp_Pnt p1(x1 - gap, y1 - gap, z1 - gap);
						gp_Pnt p2(x2 + gap, y2 + gap, z2 + gap);
						
						if (!b.IsOut(p1) && !b.IsOut(p2)) {
							ts_filtered.push_back(*it);
						}
					}
					return ts_filtered;
				} else {
					return s.results();
				}
			}

			std::vector<T> select(const T& t, bool completely_within = false, double extend = 0.0) const {
				std::vector<T> ts = select_box(t, completely_within, extend);
				if (ts.empty()) {
					return ts;
				}

				std::vector<T> ts_filtered;

				const TopoDS_Shape& A = shapes_.find(t)->second;
				if (IfcGeom::Kernel::count(A, TopAbs_SHELL) == 0) {
					return ts_filtered;
				}

				ts_filtered.reserve(ts.size());

				typename std::vector<T>::const_iterator it = ts.begin();
				for (it = ts.begin(); it != ts.end(); ++it) {
					const TopoDS_Shape& B = shapes_.find(*it)->second;
					if (IfcGeom::Kernel::count(B, TopAbs_SHELL) == 0) {
						continue;
					}

					if (test(A, B, completely_within, extend)) {
						ts_filtered.push_back(*it);
					}
				}

				return ts_filtered;
			}

			std::vector<T> select(const TopoDS_Shape& s, bool completely_within = false, double extend = -1.e-5) const {
				Bnd_Box bb;
				BRepBndLib::AddClose(s, bb);
				bb.SetGap(bb.GetGap() + extend);

				std::vector<T> ts;

				if (extend < 0.) {
					// Shell are only required when we do the boolean based intersection check
					if (IfcGeom::Kernel::count(s, TopAbs_SHELL) == 0) {
						return ts;
					}
				}

				ts = select_box(bb, completely_within);

				if (ts.empty()) {
					return ts;
				}

				std::vector<T> ts_filtered;
				ts_filtered.reserve(ts.size());

				typename std::vector<T>::const_iterator it = ts.begin();
				for (it = ts.begin(); it != ts.end(); ++it) {
					const TopoDS_Shape& B = shapes_.find(*it)->second;

					if (IfcGeom::Kernel::count(B, TopAbs_SHELL) == 0) {
						continue;
					}

					if (test(s, B, completely_within, extend)) {
						ts_filtered.push_back(*it);
					}
				}

				return ts_filtered;
			}

			std::vector<T> select(const gp_Pnt& p, double extend=0.0) const {
				std::vector<T> ts = select_box(p, extend);
				if (ts.empty()) {
					return ts;
				}

				std::vector<T> ts_filtered;
				ts_filtered.reserve(ts.size());

				TopoDS_Vertex v;
				if (extend > 0.) {
					BRep_Builder B;
					B.MakeVertex(v, p, Precision::Confusion());
				}

				typename std::vector<T>::const_iterator it = ts.begin();
				for (it = ts.begin(); it != ts.end(); ++it) {
					const TopoDS_Shape& B = shapes_.find(*it)->second;
					if (extend > 0.0) {
						BRepExtrema_DistShapeShape dss(v, B);
						if (dss.Perform() && dss.NbSolution() >= 1) {
							ts_filtered.push_back(*it);
						}
					} else {
						TopExp_Explorer exp(B, TopAbs_SOLID);
						for (; exp.More(); exp.Next()) {
							BRepClass3d_SolidClassifier cls(exp.Current(), p, 1e-5);
							if (cls.State() != TopAbs_OUT) {
								ts_filtered.push_back(*it);
								break;
							}
						}
					}
				}

				return ts_filtered;
			}

		protected:

			typedef NCollection_UBTree<T, Bnd_Box> tree_t;
			typedef std::map<T, TopoDS_Shape> map_t;
			tree_t tree_;
			map_t shapes_;

			class selector : public tree_t::Selector
			{
			public:
				selector(const Bnd_Box& b)
					: tree_t::Selector()
					, bounds_(b)
				{}

				Standard_Boolean Reject(const Bnd_Box& b) const {
					return bounds_.IsOut(b);
				}

				Standard_Boolean Accept(const T& o) {
					results_.push_back(o);
					return Standard_True;
				}

				const std::vector<T>& results() const {
					return results_;
				}

			private:
				std::vector<T> results_;
				const Bnd_Box& bounds_;
			};

		};
	}

	class tree : public impl::tree<IfcUtil::IfcBaseEntity*> {
	public:

		tree() {};

		tree(IfcParse::IfcFile& f) {
			add_file(f, IfcGeom::IteratorSettings());
		}

		tree(IfcParse::IfcFile& f, const IfcGeom::IteratorSettings& settings) {
			add_file(f, settings);
		}

		tree(IfcGeom::Iterator<double>& it) {
			add_file(it);
		}		

		void add_file(IfcParse::IfcFile& f, const IfcGeom::IteratorSettings& settings) {
			IfcGeom::IteratorSettings settings_ = settings;
			settings_.set(IfcGeom::IteratorSettings::DISABLE_TRIANGULATION, true);
			settings_.set(IfcGeom::IteratorSettings::USE_WORLD_COORDS, true);
			settings_.set(IfcGeom::IteratorSettings::SEW_SHELLS, true);

			IfcGeom::Iterator<double> it(settings_, &f);

			add_file(it);
		}

		void add_file(IfcGeom::Iterator<double>& it) {
			if (it.initialize()) {
				do {
					add_element(dynamic_cast<IfcGeom::BRepElement<double>*>(it.get()));
				} while (it.next());
			}
		}

		void add_element(IfcGeom::BRepElement<double>* elem) {
			if (!elem) {
				return;
			}
			auto compound = elem->geometry().as_compound();
			compound.Move(elem->transformation().data());
			add(elem->product(), compound);
		}
	};

}

#endif
