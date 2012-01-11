// Area.cpp

// Copyright 2011, Dan Heeks
// This program is released under the BSD license. See the file COPYING for details.

#include <cstdio>
#include "Area.h"
#include "AreaOrderer.h"

#include "TestMacros.h"

double CArea::m_accuracy = 0.01;
double CArea::m_units = 1.0;
bool CArea::m_fit_arcs = true;
double CArea::m_single_area_processing_length = 0.0;
double CArea::m_processing_done = 0.0;
bool CArea::m_please_abort = false;
double CArea::m_MakeOffsets_increment = 0.0;
double CArea::m_split_processing_length = 0.0;
bool CArea::m_set_processing_length_in_split = false;
double CArea::m_after_MakeOffsets_length = 0.0;
static const double PI = 3.1415926535897932;

void CArea::append(const CCurve& curve)
{
	m_curves.push_back(curve);
}

void CArea::FitArcs(){
	for(std::list<CCurve>::iterator It = m_curves.begin(); It != m_curves.end(); It++)
	{
		CCurve& curve = *It;
		curve.FitArcs();
	}
}

Point CArea::NearestPoint(const Point& p)const
{
	double best_dist = 0.0;
	Point best_point = Point(0, 0);
	for(std::list<CCurve>::const_iterator It = m_curves.begin(); It != m_curves.end(); It++)
	{
		const CCurve& curve = *It;
		Point near_point = curve.NearestPoint(p);
		double dist = near_point.dist(p);
		if(It == m_curves.begin() || dist < best_dist)
		{
			best_dist = dist;
			best_point = near_point;
		}
	}
	return best_point;
}

void CArea::GetBox(CAreaBox &box)
{
	for(std::list<CCurve>::iterator It = m_curves.begin(); It != m_curves.end(); It++)
	{
		CCurve& curve = *It;
		curve.GetBox(box);
	}
}

void CArea::Reorder()
{
  dprintf("entered ...\n");
	// curves may have been added with wrong directions
	// test all kurves to see which one are outsides and which are insides and 
	// make sure outsides are anti-clockwise and insides are clockwise

	// returns 0, if the curves are OK
	// returns 1, if the curves are overlapping

	CAreaOrderer ao;
  int curve_num = 0;
  dprintf("processing %zd curves ...\n", m_curves.size());
	for(std::list<CCurve>::iterator It = m_curves.begin(); It != m_curves.end(); It++)
	{
    curve_num++;
		CCurve& curve = *It;
    dprintf("(curve %d/%zd) inserting curve into orderer ...\n", curve_num, m_curves.size());
		ao.Insert(&curve);
    dprintf("(curve %d/%zd) ... done inserting curve into orderer.\n", curve_num, m_curves.size());
		if(m_set_processing_length_in_split)
		{
			CArea::m_processing_done += (m_split_processing_length / m_curves.size());
		}
	}
  dprintf("... done processing %zd curves.\n", m_curves.size());
  dprintf("resetting area data ...\n");
	*this = ao.ResultArea();
  dprintf("... done resetting area data.\n");
  dprintf("... done.\n");
}

class ZigZag
{
public:
	CCurve zig;
	CCurve zag;
	ZigZag(const CCurve& Zig, const CCurve& Zag):zig(Zig), zag(Zag){}
};

static double stepover_for_pocket = 0.0;
static std::list<ZigZag> zigzag_list_for_zigs;
static std::list<CCurve> *curve_list_for_zigs = NULL;
static bool rightward_for_zigs = true;
static double sin_angle_for_zigs = 0.0;
static double cos_angle_for_zigs = 0.0;
static double sin_minus_angle_for_zigs = 0.0;
static double cos_minus_angle_for_zigs = 0.0;
static double one_over_units = 0.0;

static Point rotated_point(const Point &p)
{
	return Point(p.x * cos_angle_for_zigs - p.y * sin_angle_for_zigs, p.x * sin_angle_for_zigs + p.y * cos_angle_for_zigs);
}
    
static Point unrotated_point(const Point &p)
{
    return Point(p.x * cos_minus_angle_for_zigs - p.y * sin_minus_angle_for_zigs, p.x * sin_minus_angle_for_zigs + p.y * cos_minus_angle_for_zigs);
}

static CVertex rotated_vertex(const CVertex &v)
{
	if(v.m_type)
	{
		return CVertex(v.m_type, rotated_point(v.m_p), rotated_point(v.m_c));
	}
    return CVertex(v.m_type, rotated_point(v.m_p), Point(0, 0));
}

static CVertex unrotated_vertex(const CVertex &v)
{
	if(v.m_type)
	{
		return CVertex(v.m_type, unrotated_point(v.m_p), unrotated_point(v.m_c));
	}
	return CVertex(v.m_type, unrotated_point(v.m_p), Point(0, 0));
}

static void rotate_area(CArea &a)
{
	for(std::list<CCurve>::iterator It = a.m_curves.begin(); It != a.m_curves.end(); It++)
	{
		CCurve& curve = *It;
		for(std::list<CVertex>::iterator CIt = curve.m_vertices.begin(); CIt != curve.m_vertices.end(); CIt++)
		{
			CVertex& vt = *CIt;
			vt = rotated_vertex(vt);
		}
	}
}

void test_y_point(int i, const Point& p, Point& best_p, bool &found, int &best_index, double y, bool left_not_right)
{
	// only consider points at y
	if(fabs(p.y - y) < 0.002 * one_over_units)
	{
		if(found)
		{
			// equal high point
			if(left_not_right)
			{
				// use the furthest left point
				if(p.x < best_p.x)
				{
					best_p = p;
					best_index = i;
				}
			}
			else
			{
				// use the furthest right point
				if(p.x > best_p.x)
				{
					best_p = p;
					best_index = i;
				}
			}
		}
		else
		{
			best_p = p;
			best_index = i;
			found = true;
		}
	}
}

static void make_zig_curve(const CCurve& input_curve, double y0, double y)
{
	CCurve curve(input_curve);

	if(rightward_for_zigs)
	{
		if(curve.IsClockwise())
			curve.Reverse();
	}
	else
	{
		if(!curve.IsClockwise())
			curve.Reverse();
	}

    // find a high point to start looking from
	Point top_left;
	int top_left_index;
	bool top_left_found = false;
	Point top_right;
	int top_right_index;
	bool top_right_found = false;
	Point bottom_left;
	int bottom_left_index;
	bool bottom_left_found = false;

	int i =0;
	for(std::list<CVertex>::const_iterator VIt = curve.m_vertices.begin(); VIt != curve.m_vertices.end(); VIt++, i++)
	{
		const CVertex& vertex = *VIt;

		test_y_point(i, vertex.m_p, top_right, top_right_found, top_right_index, y, !rightward_for_zigs);
		test_y_point(i, vertex.m_p, top_left, top_left_found, top_left_index, y, rightward_for_zigs);
		test_y_point(i, vertex.m_p, bottom_left, bottom_left_found, bottom_left_index, y0, rightward_for_zigs);
	}

	int start_index = 0;
	int end_index = 0;
	int zag_end_index = 0;

	if(bottom_left_found)start_index = bottom_left_index;
	else if(top_left_found)start_index = top_left_index;

	if(top_right_found)
	{
		end_index = top_right_index;
		zag_end_index = top_left_index;
	}
	else
	{
		end_index = bottom_left_index;
		zag_end_index =  bottom_left_index;
	}
	if(end_index <= start_index)end_index += (i-1);
	if(zag_end_index <= start_index)zag_end_index += (i-1);

    CCurve zig, zag;
    
    bool zig_started = false;
    bool zig_finished = false;
    bool zag_finished = false;
    
	int v_index = 0;
	for(int i = 0; i < 2; i++)
	{
		// process the curve twice because we don't know where it will start
		if(zag_finished)
			break;
		for(std::list<CVertex>::const_iterator VIt = curve.m_vertices.begin(); VIt != curve.m_vertices.end(); VIt++)
		{
			if(i == 1 && VIt == curve.m_vertices.begin())
			{
				continue;
			}

			const CVertex& vertex = *VIt;

			if(zig_finished)
			{
				zag.m_vertices.push_back(unrotated_vertex(vertex));
				if(v_index == zag_end_index)
				{
					zag_finished = true;
					break;
				}
			}
			else if(zig_started)
			{
				zig.m_vertices.push_back(unrotated_vertex(vertex));
				if(v_index == end_index)
				{
					zig_finished = true;
					if(v_index == zag_end_index)
					{
						zag_finished = true;
						break;
					}
					zag.m_vertices.push_back(unrotated_vertex(vertex));
				}
			}
			else
			{
				if(v_index == start_index)
				{
					zig.m_vertices.push_back(unrotated_vertex(vertex));
					zig_started = true;
				}
			}
			v_index++;
		}
	}
        
    if(zig_finished)
		zigzag_list_for_zigs.push_back(ZigZag(zig, zag));
}

void make_zig(const CArea &a, double y0, double y)
{
	for(std::list<CCurve>::const_iterator It = a.m_curves.begin(); It != a.m_curves.end(); It++)
	{
		const CCurve &curve = *It;
		make_zig_curve(curve, y0, y);
	}
}
        
std::list< std::list<ZigZag> > reorder_zig_list_list;
        
void add_reorder_zig(ZigZag &zigzag)
{
    // look in existing lists

	// see if the zag is part of an existing zig
	if(zigzag.zag.m_vertices.size() > 1)
	{
		const Point& zag_e = zigzag.zag.m_vertices.front().m_p;
		bool zag_removed = false;
		for(std::list< std::list<ZigZag> >::iterator It = reorder_zig_list_list.begin(); It != reorder_zig_list_list.end() && !zag_removed; It++)
		{
			std::list<ZigZag> &zigzag_list = *It;
			for(std::list<ZigZag>::iterator It2 = zigzag_list.begin(); It2 != zigzag_list.end() && !zag_removed; It2++)
			{
				const ZigZag& z = *It2;
				for(std::list<CVertex>::const_iterator It3 = z.zig.m_vertices.begin(); It3 != z.zig.m_vertices.end() && !zag_removed; It3++)
				{
					const CVertex &v = *It3;
					if((fabs(zag_e.x - v.m_p.x) < (0.002 * one_over_units)) && (fabs(zag_e.y - v.m_p.y) < (0.002 * one_over_units)))
					{
						// remove zag from zigzag
						zigzag.zag.m_vertices.clear();
						zag_removed = true;
					}
				}
			}
		}
	}

	// see if the zigzag can join the end of an existing list
	const Point& zig_s = zigzag.zig.m_vertices.front().m_p;
	for(std::list< std::list<ZigZag> >::iterator It = reorder_zig_list_list.begin(); It != reorder_zig_list_list.end(); It++)
	{
		std::list<ZigZag> &zigzag_list = *It;
		const ZigZag& last_zigzag = zigzag_list.back();
        const Point& e = last_zigzag.zig.m_vertices.back().m_p;
        if((fabs(zig_s.x - e.x) < (0.002 * one_over_units)) && (fabs(zig_s.y - e.y) < (0.002 * one_over_units)))
		{
            zigzag_list.push_back(zigzag);
			return;
		}
	}
        
    // else add a new list
    std::list<ZigZag> zigzag_list;
    zigzag_list.push_back(zigzag);
    reorder_zig_list_list.push_back(zigzag_list);
}

void reorder_zigs()
{
	for(std::list<ZigZag>::iterator It = zigzag_list_for_zigs.begin(); It != zigzag_list_for_zigs.end(); It++)
	{
		ZigZag &zigzag = *It;
        add_reorder_zig(zigzag);
	}
        
	zigzag_list_for_zigs.clear();

	for(std::list< std::list<ZigZag> >::iterator It = reorder_zig_list_list.begin(); It != reorder_zig_list_list.end(); It++)
	{
		std::list<ZigZag> &zigzag_list = *It;
		if(zigzag_list.size() == 0)continue;

		curve_list_for_zigs->push_back(CCurve());
		for(std::list<ZigZag>::const_iterator It = zigzag_list.begin(); It != zigzag_list.end();)
		{
			const ZigZag &zigzag = *It;
			for(std::list<CVertex>::const_iterator It2 = zigzag.zig.m_vertices.begin(); It2 != zigzag.zig.m_vertices.end(); It2++)
			{
				if(It2 == zigzag.zig.m_vertices.begin() && It != zigzag_list.begin())continue; // only add the first vertex if doing the first zig
				const CVertex &v = *It2;
				curve_list_for_zigs->back().m_vertices.push_back(v);
			}

			It++;
			if(It == zigzag_list.end())
			{
				for(std::list<CVertex>::const_iterator It2 = zigzag.zag.m_vertices.begin(); It2 != zigzag.zag.m_vertices.end(); It2++)
				{
					if(It2 == zigzag.zag.m_vertices.begin())continue; // don't add the first vertex of the zag
					const CVertex &v = *It2;
					curve_list_for_zigs->back().m_vertices.push_back(v);
				}
			}
		}
	}
	reorder_zig_list_list.clear();
}

static void zigzag(const CArea &input_a)
{
	if(input_a.m_curves.size() == 0)
	{
		CArea::m_processing_done += CArea::m_single_area_processing_length;
		return;
	}
    
    one_over_units = 1 / CArea::m_units;
    
	CArea a(input_a);
    rotate_area(a);
    
    CAreaBox b;
	a.GetBox(b);
    
    double x0 = b.MinX() - 1.0;
    double x1 = b.MaxX() + 1.0;

    double height = b.MaxY() - b.MinY();
    int num_steps = int(height / stepover_for_pocket + 1);
    double y = b.MinY();// + 0.1 * one_over_units;
    Point null_point(0, 0);
	rightward_for_zigs = true;

	if(CArea::m_please_abort)return;

	double step_percent_increment = 0.8 * CArea::m_single_area_processing_length / num_steps;

	for(int i = 0; i<num_steps; i++)
	{
		double y0 = y;
		y = y + stepover_for_pocket;
		Point p0(x0, y0);
		Point p1(x0, y);
		Point p2(x1, y);
		Point p3(x1, y0);
		CCurve c;
		c.m_vertices.push_back(CVertex(0, p0, null_point, 0));
		c.m_vertices.push_back(CVertex(0, p1, null_point, 0));
		c.m_vertices.push_back(CVertex(0, p2, null_point, 1));
		c.m_vertices.push_back(CVertex(0, p3, null_point, 0));
		c.m_vertices.push_back(CVertex(0, p0, null_point, 1));
		CArea a2;
		a2.m_curves.push_back(c);
		a2.Intersect(a);
		make_zig(a2, y0, y);
		rightward_for_zigs = !rightward_for_zigs;
		if(CArea::m_please_abort)return;
		CArea::m_processing_done += step_percent_increment;
	}

	reorder_zigs();
	CArea::m_processing_done += 0.2 * CArea::m_single_area_processing_length;
}

void CArea::SplitAndMakePocketToolpath(std::list<CCurve> &curve_list, const CAreaPocketParams &params)const
{
  dprintf("entered ...\n");
	CArea::m_processing_done = 0.0;

	double save_units = CArea::m_units;
	CArea::m_units = 1.0;
	std::list<CArea> areas;
	m_split_processing_length = 50.0; // jump to 50 percent after split
	m_set_processing_length_in_split = true;
  dprintf("Split() ...\n");
	Split(areas);
  dprintf(".. Split() done.\n");
	m_set_processing_length_in_split = false;
	CArea::m_processing_done = m_split_processing_length;
	CArea::m_units = save_units;

	if(areas.size() == 0)return;

	double single_area_length = 50.0 / areas.size();

  int area_num = 0;
  dprintf("processing %zd areas ...\n", areas.size());
	for(std::list<CArea>::iterator It = areas.begin(); It != areas.end(); It++)
	{
    area_num++;
		CArea::m_single_area_processing_length = single_area_length;
		CArea &ar = *It;
    dprintf("(area %d/%zd) MakePocketToolpath() ...\n", area_num, areas.size());
		ar.MakePocketToolpath(curve_list, params);
    dprintf("(area %d/%zd) ... MakePocketToolpath() done.\n", area_num, areas.size());
	}
  dprintf("... done processing %zd areas.\n", areas.size());
  dprintf("... done.\n");
}

void CArea::MakePocketToolpath(std::list<CCurve> &curve_list, const CAreaPocketParams &params)const
{
  dprintf("entered ...\n");
	double radians_angle = params.zig_angle * PI / 180;
	sin_angle_for_zigs = sin(-radians_angle);
	cos_angle_for_zigs = cos(-radians_angle);
	sin_minus_angle_for_zigs = sin(radians_angle);
	cos_minus_angle_for_zigs = cos(radians_angle);
	stepover_for_pocket = params.stepover;

	CArea a_offset = *this;
	double current_offset = params.tool_radius + params.extra_offset;

	a_offset.Offset(current_offset);

	if(params.mode == ZigZagPocketMode || params.mode == ZigZagThenSingleOffsetPocketMode)
	{
		curve_list_for_zigs = &curve_list;
		zigzag(a_offset);
	}
	else if(params.mode == SpiralPocketMode)
	{
		std::list<CArea> m_areas;
		a_offset.Split(m_areas);
		if(CArea::m_please_abort)return;
		if(m_areas.size() == 0)
		{
			CArea::m_processing_done += CArea::m_single_area_processing_length;
			return;
		}

		CArea::m_single_area_processing_length /= m_areas.size();

    int area_num = 0;
    dprintf("spiral-pocketing %zd areas ...\n", m_areas.size());
		for(std::list<CArea>::iterator It = m_areas.begin(); It != m_areas.end(); It++)
		{
      area_num++;
			CArea &a2 = *It;
      dprintf("(area %d/%zd) MakeOnePocketCurve() ...\n", area_num, m_areas.size());
			a2.MakeOnePocketCurve(curve_list, params);
      dprintf("(area %d/%zd) ... MakeOnePocketCurve() done.\n", area_num, m_areas.size());
		}
    dprintf("... done spiral-pocketing %zd areas.\n", m_areas.size());
	}

	if(params.mode == SingleOffsetPocketMode || params.mode == ZigZagThenSingleOffsetPocketMode)
	{
    dprintf("processing single offset ...\n");
		// add the single offset too
		for(std::list<CCurve>::iterator It = a_offset.m_curves.begin(); It != a_offset.m_curves.end(); It++)
		{
			CCurve& curve = *It;
			curve_list.push_back(curve);
		}
    dprintf("... processing single offset done.\n");
	}
  dprintf("... done.\n");
}

void CArea::Split(std::list<CArea> &m_areas)const
{
  dprintf("entered ...\n");
	if(HolesLinked())
	{
    int curve_num = 0;
    dprintf("HolesLinked() returns true; processing %zd curves ...\n", m_curves.size());
		for(std::list<CCurve>::const_iterator It = m_curves.begin(); It != m_curves.end(); It++)
		{
      curve_num++;
			const CCurve& curve = *It;
			m_areas.push_back(CArea());
			m_areas.back().m_curves.push_back(curve);
		}
    dprintf("... done processing %zd curves.\n", m_curves.size());
	}
	else
	{
    dprintf("HolesLinked() returns false ...\n");
		CArea a = *this;
    dprintf("Reorder() ...\n");
		a.Reorder();
    dprintf("... Reorder() done.\n");

		if(CArea::m_please_abort)return;

    int curve_num = 0;
    dprintf("processing %zd curves ...\n", a.m_curves.size());
		for(std::list<CCurve>::const_iterator It = a.m_curves.begin(); It != a.m_curves.end(); It++)
		{
      curve_num++;
      dprintf("(curve %d/%zd) checking IsClockwise() ...\n", curve_num, a.m_curves.size());
			const CCurve& curve = *It;
			if(curve.IsClockwise())
			{
        dprintf("(curve %d/%zd) ... IsClockwise() returns true.\n", curve_num, a.m_curves.size());
        dprintf("(curve %d/%zd) checking whether to push curve to area's curve array ...\n", curve_num, a.m_curves.size());
				if(m_areas.size() > 0){
          dprintf("(curve %d/%zd) ... yep; pushing curve into last area ...\n", curve_num, a.m_curves.size());
					m_areas.back().m_curves.push_back(curve);
        } else {
          dprintf("(curve %d/%zd) ... nope.\n", curve_num, a.m_curves.size());
        }
			}
			else
			{
        dprintf("(curve %d/%zd) ... IsClockwise() returns false; pushing curve into new area ...\n", curve_num, a.m_curves.size());
				m_areas.push_back(CArea());
				m_areas.back().m_curves.push_back(curve);
        dprintf("(curve %d/%zd) ... done pushing curve into new area.\n", curve_num, a.m_curves.size());
			}
		}
    dprintf("... done processing %zd curves.\n", m_curves.size());
	}
  dprintf("... done.\n");
}

double CArea::GetArea(bool always_add)const
{
	// returns the area of the area
	double area = 0.0;
	for(std::list<CCurve>::const_iterator It = m_curves.begin(); It != m_curves.end(); It++)
	{
		const CCurve& curve = *It;
		double a = curve.GetArea();
		if(always_add)area += fabs(a);
		else area += a;
	}
	return area;
}

void DetailSpan(Span &span)
{
  dprintf("  span: is_start_span:%d, p:(%g, %g), v.type:%d, v.p:(%g, %g), v.c:(%g, %g)\n",
    span.m_start_span,
    span.m_p.x, span.m_p.y,
    span.m_v.m_type,
    span.m_v.m_p.x, span.m_v.m_p.y,
    span.m_v.m_c.x, span.m_v.m_c.y
  );
}

void DetailVertex(CVertex &vertex)
{
  dprintf("  vertex: type:%d, p:(%g, %g), c:(%g, %g)\n",
    vertex.m_type,
    vertex.m_p.x, vertex.m_p.y,
    vertex.m_c.x, vertex.m_c.y
  );
}

void DetailCurve(CCurve &curve)
{
  dprintf("entered ...\n");
  dprintf("new curve ...\n");
  dprintf("IsClosed():%d\n", curve.IsClosed());
  dprintf("IsClockwise():%d\n", curve.IsClockwise());
  dprintf("as vertices:\n");
  for(std::list<CVertex>::iterator v = curve.m_vertices.begin(); v != curve.m_vertices.end(); v++){
    DetailVertex(*v);
  }
  dprintf("as spans:\n");
  std::list<Span> spans;
  curve.GetSpans(spans);
  for(std::list<Span>::iterator s = spans.begin(); s != spans.end(); s++){
    DetailSpan(*s);
  }
  dprintf("... done.\n");
}

void DetailArea(CArea &area)
{
  dprintf("entered ...\n");
  for(std::list<CCurve>::iterator c = area.m_curves.begin(); c != area.m_curves.end(); c++){
    dprintf("new curve ...\n");
    DetailCurve(*c);
  }
  dprintf("... done.\n");
}


/*
This is a minimal version of the recur() function in HeeksCNC's area_funcs.py. 
*/
void CArea::PocketRecursion(std::list<CArea> &arealist, const CAreaPocketParams &params, int depth){
  dprintf("(depth %d) entered ...\n", depth);
  DetailArea(*this);
  if(params.from_center) { arealist.push_front(*this); }
  else { arealist.push_back(*this); }

  dprintf("(depth %d) instantiating new offset area ...\n", depth);
  CArea a_offs(*this);
  dprintf("(depth %d) ... done instantiating new offset area.\n", depth);
  dprintf("(depth %d) Offset() by %g ...\n", depth, params.stepover);
  a_offs.Offset(params.stepover);
  dprintf("(depth %d) ... Offset() done.\n", depth);

  int curve_num = 0;
  dprintf("(depth %d) processing %zd curves ...\n", depth, a_offs.m_curves.size());
  for(std::list<CCurve>::iterator c = a_offs.m_curves.begin(); c != a_offs.m_curves.end(); c++){
    curve_num++;
    dprintf("(depth %d) (curve %d/%zd) instantiatng new area ...\n", depth, curve_num, a_offs.m_curves.size());
    CArea a;
    a.append(*c);
    dprintf("(depth %d) (curve %d/%zd) PocketRecursion() ...\n", depth, curve_num, a_offs.m_curves.size());
    a.PocketRecursion(arealist, params, depth+1);
    dprintf("(depth %d) (curve %d/%zd) ... PocketRecursion() done.\n", depth, curve_num, a_offs.m_curves.size());
  }
  dprintf("(depth %d) ... done processing curves.\n", depth);
  dprintf("(depth %d) ... done.\n", depth);
}

/*
This is a minimal version of the pocket() function in HeeksCNC's area_funcs.py. 
*/
void CArea::RecursivePocket(std::list<CCurve> &toolpath, const CAreaPocketParams &params, bool recurse){
  dprintf("entered ...\n");
  std::list<CArea> arealist;

  dprintf("instantiating new offset area ...\n");
  CArea a_offs(*this);
  dprintf("... done instantiating new offset area.\n");
  dprintf("Offset() by %g ...\n", params.tool_radius + params.extra_offset);
  a_offs.Offset(params.tool_radius + params.extra_offset);
  dprintf("... Offset() done.\n");
  dprintf("PocketRecursion() ...\n");
  a_offs.PocketRecursion(arealist, params);
  dprintf("... PocketRecursion() done.\n");

  int area_num = 0;
  dprintf("collecting curves from %zd ares ...\n", arealist.size());
  for(std::list<CArea>::iterator a = arealist.begin(); a != arealist.end(); a++){
    area_num++;
    dprintf("(area %d/%zd) collecting %zd curves ...\n", area_num, arealist.size(), a->m_curves.size());
    for(std::list<CCurve>::iterator c = a->m_curves.begin(); c != a->m_curves.end(); c++){
      toolpath.push_back(*c);
    }
    dprintf("(area %d/%zd) ... done collecting curves.\n", area_num, arealist.size());
  }
  dprintf("... done collecting curves from areas.\n");
  dprintf("... done.\n");
}


eOverlapType GetOverlapType(const CCurve& c1, const CCurve& c2)
{
	CArea a1;
	a1.m_curves.push_back(c1);
	CArea a2;
	a2.m_curves.push_back(c2);

	return GetOverlapType(a1, a2);
}

eOverlapType GetOverlapType(const CArea& a1, const CArea& a2)
{
	CArea A1(a1);

	A1.Subtract(a2);
	if(A1.m_curves.size() == 0)
	{
		return eInside;
	}

	CArea A2(a2);
	A2.Subtract(a1);
	if(A2.m_curves.size() == 0)
	{
		return eOutside;
	}

	A1 = a1;
	A1.Intersect(a2);
	if(A1.m_curves.size() == 0)
	{
		return eSiblings;
	}

	return eCrossing;
}

bool IsInside(const Point& p, const CCurve& c)
{
	CArea a;
	a.m_curves.push_back(c);
	return IsInside(p, a);
}

bool IsInside(const Point& p, const CArea& a)
{
	CArea a2;
	CCurve c;
	c.m_vertices.push_back(CVertex(Point(p.x - 0.01, p.y - 0.01)));
	c.m_vertices.push_back(CVertex(Point(p.x + 0.01, p.y - 0.01)));
	c.m_vertices.push_back(CVertex(Point(p.x + 0.01, p.y + 0.01)));
	c.m_vertices.push_back(CVertex(Point(p.x - 0.01, p.y + 0.01)));
	c.m_vertices.push_back(CVertex(Point(p.x - 0.01, p.y - 0.01)));
	a2.m_curves.push_back(c);
	a2.Intersect(a);
	if(fabs(a2.GetArea()) < 0.0004)return false;
	return true;
}
