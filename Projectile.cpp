#include "pch.h"
#include "Projectile.h"
#include "Utility.h"

static const float HIT_DISTANCE = 10.f;

static const Vector2f LEFT(-1.0f, 0.0f);

Projectile::Projectile(std::weak_ptr<Enemy> target, float power, float speed)
: speed(speed), power(power), target(target), hit(false)
{
	std::shared_ptr<Enemy> tgt = target.lock();

	if (tgt) {
		targetPosition = tgt->GetPosition();
	}
}


void Projectile::SetImage(const Image& img)
{
	AnimSprite::SetImage(img);
	SetCenter(img.GetWidth() / 2.0f, img.GetHeight() / 2.0f);
}

void Projectile::Update(float elapsed)
{
	AnimSprite::Update(elapsed);

	if (hit)
		return;

	std::shared_ptr<Enemy> tgt = target.lock();

	if (tgt)
		targetPosition = tgt->GetPosition();

	Vector2f dir = targetPosition - GetPosition();
	float r = norm(dir);
	if (r < HIT_DISTANCE) {
		hit = true;
		Hit(tgt);
		return;
	}

	dir /= r;
	float angle = acosf(dot(LEFT, dir));
	if (dir.y < 0)
		angle = -angle;

	Move(dir * speed * elapsed);
	SetRotation(angle * 180/PI);
}

void Projectile::Hit(std::shared_ptr<Enemy>& tgt)
{
	if (tgt)
		tgt->Hit(power);
}
