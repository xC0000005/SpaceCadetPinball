#pragma once
#include "maths.h"
#include "TEdgeSegment.h"

class TLine :
	public TEdgeSegment
{
public:
	line_type Line{};
	float X0, Y0, X1, Y1;
	TLine(TCollisionComponent* collCmp, char* activeFlag, unsigned int collisionGroup, float x0, float y0, float x1, float y1);
	TLine(TCollisionComponent* collCmp, char* activeFlag, unsigned int collisionGroup, const vector2& start, const vector2& end);
	void Offset(float offset);
	float FindCollisionDistance(ray_type* ray) override;
	void EdgeCollision(TBall* ball, float distance) override;
	void place_in_grid(RectF* aabb) override;
};
