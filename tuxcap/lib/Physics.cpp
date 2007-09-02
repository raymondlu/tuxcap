/* W.P. van Paassen */

#include "Physics.h"
#include <assert.h>
#include <stdlib.h>

using namespace Sexy;

PhysicsListener* Physics::listener = NULL;


Physics::Physics():space(NULL),steps(1){
    cpInitChipmunk();
}

Physics::~Physics() {
  Clear();
}

void Physics::Init() {
  if (space == NULL) {
    cpResetShapeIdCounter();
    space = cpSpaceNew();
    assert(space != NULL);

    cpSpaceSetDefaultCollisionPairFunc(space, NULL, NULL);

    space->gravity = cpv(0,100);

    delta = 1.0f/60.0f/(cpFloat)steps;
  }
}

void Physics::SetSteps(int steps) {
  this->steps = steps;
  delta = 1.0f/60.0f/(cpFloat)steps;
}

int Physics::CollFunc(cpShape *a, cpShape *b, cpContact *contacts, int numContacts, void *data) {
  assert(listener != NULL);

  PhysicsObject obj1(a->body, a);
  PhysicsObject obj2(b->body, b);

  std::vector<CollisionPoint> points;
  for (int i = 0; i < numContacts; ++i) {
    points.push_back(CollisionPoint(SexyVector2(contacts[i].p.x, contacts[i].p.y), 
                                    SexyVector2(contacts[i].n.x, contacts[i].n.y),contacts[i].dist));
  }
  CollisionObject col(obj1, obj2, points); 
  listener->HandleTypedCollision(&col);
}

void Physics::AllCollisions(void* ptr, void* data) { 
  assert(listener != NULL && ptr != NULL);
  cpArbiter *arb = static_cast<cpArbiter*>(ptr);

  //cpVect sum_impulse = cpContactsSumImpulses(arb->contacts, arb->numContacts);
  //cpVect sum_impulse_with_friction = cpContactsSumImpulsesWithFriction(arb->contacts, arb->numContacts);

  PhysicsObject obj1(arb->a->body, arb->a);
  PhysicsObject obj2(arb->b->body, arb->b);

  std::vector<CollisionPoint> points;
  for (int i = 0; i < arb->numContacts; ++i) {
    points.push_back(CollisionPoint(SexyVector2(arb->contacts[i].p.x, arb->contacts[i].p.y), 
                                    SexyVector2(arb->contacts[i].n.x, arb->contacts[i].n.y),arb->contacts[i].dist));
  }
  CollisionObject col(obj1, obj2, points); 
  listener->HandleCollision(&col);
}

void Physics::HashQuery(void* ptr, void* data) { 
  assert(listener != NULL && ptr != NULL);
  cpShape* shape = static_cast<cpShape*>(ptr);
  PhysicsObject obj(shape->body, shape);
  listener->DrawPhysicsObject(&obj, static_cast<Graphics*>(data));
}

void Physics::Update() {
#if 0
  static int count = 0;

  if (++count == 10) {
    count = 0;
  }
  else 
#endif
{
  assert(listener != NULL);
  for(int i=0; i<steps; i++){
    listener->BeforePhysicsStep();
    cpSpaceStep(space, delta);
    listener->AfterPhysicsStep();
  }

  cpArrayEach(space->arbiters, &AllCollisions, NULL);
  }
}

void Physics::Draw(Graphics* g) {
  cpSpaceHashEach(space->activeShapes, &HashQuery, g);
  cpSpaceHashEach(space->staticShapes, &HashQuery, g);
}

void Physics::SetGravity(const SexyVector2& gravity) {
  assert(space != NULL);
  space->gravity = cpv(gravity.x, gravity.y);
}

void Physics::SetDamping(cpFloat damping) {
  assert(space != NULL);
  space->damping = damping;
}

void Physics::SetIterations(int iter) {
  assert(space != NULL);
  space->iterations = iter;
}

void Physics::ResizeStaticHash(float dimension, int count) {
  assert(space != NULL);
  cpSpaceResizeStaticHash(space, dimension, count);
}

void Physics::ResizeActiveHash(float dimension, int count) {
  assert(space != NULL);
  cpSpaceResizeStaticHash(space, dimension, count);
}

void Physics::Clear() {
  std::vector<PhysicsObject*>::iterator it = objects.begin();
  while (it != objects.end()) {
    delete (*it);
    ++it;
  }
  objects.clear();
  cpSpaceFreeChildren(space);
  cpSpaceFree(space);
  space = NULL;
}

PhysicsObject* Physics::CreateObject(cpFloat mass, cpFloat inertia) {
 PhysicsObject* obj = new PhysicsObject(mass, inertia, this);
 objects.push_back(obj);
 return obj;
}

PhysicsObject* Physics::CreateStaticObject() {
  assert(IsInitialized());
  PhysicsObject* obj = new PhysicsObject(INFINITY, INFINITY, this, true);
 objects.push_back(obj);
 return obj;
}

void Physics::DestroyObject(PhysicsObject* object) { 
  if (!object->shapes.empty()) {
    if (object->is_static) {
      std::vector<cpShape*>::iterator it = object->shapes.begin();
      while (it != object->shapes.end()) {
        cpSpaceRemoveStaticShape(space, *it);
        ++it;
      }
    }
    else  {
      std::vector<cpShape*>::iterator it = object->shapes.begin();
      while (it != object->shapes.end()) {
        cpSpaceRemoveShape(space, *it);
        ++it;
      }
    }
  }
  if (object->body != NULL)
    cpSpaceRemoveBody(space, object->body);

  std::vector<PhysicsObject*>::iterator it = objects.begin();
  while (it != objects.end()) {
    if ((*it) == object) {
      delete (*it);
      objects.erase(it);      
      return;
    }
    ++it;
  }
}

void Physics::RegisterCollisionType(unsigned long type_a, unsigned long type_b, void* data) {
  cpSpaceAddCollisionPairFunc(space, type_a, type_b, &CollFunc, data);
}

void Physics::UnregisterCollisionType(unsigned long type_a, unsigned long type_b) {
  cpSpaceRemoveCollisionPairFunc(space, type_a, type_b);
}

void Physics::ApplySpringForce(PhysicsObject* obj1, PhysicsObject* obj2, const SexyVector2& anchor1, const SexyVector2& anchor2, float rest_length, float spring, float damping) {
  cpDampedSpring(obj1->body, obj2->body, cpv(anchor1.x, anchor1.y), cpv(anchor2.x, anchor2.y), rest_length, spring, damping, delta);
}

void Physics::CreatePinJoint(const PhysicsObject* obj1, const PhysicsObject* obj2, const SexyVector2& anchor1, const SexyVector2& anchor2) {
  cpJoint* joint = cpPinJointNew(obj1->body, obj2->body, cpv(anchor1.x, anchor1.y), cpv(anchor2.x, anchor2.y));
  cpSpaceAddJoint(space,joint); 
}

void Physics::CreateSlideJoint(const PhysicsObject* obj1, const PhysicsObject* obj2, const SexyVector2& anchor1, const SexyVector2& anchor2, float min, float max) {
  cpJoint* joint = cpSlideJointNew(obj1->body, obj2->body, cpv(anchor1.x, anchor1.y), cpv(anchor2.x, anchor2.y), min, max);
  cpSpaceAddJoint(space,joint); 
}

void Physics::CreatePivotJoint(const PhysicsObject* obj1, const PhysicsObject* obj2, const SexyVector2& pivot) {
  cpJoint* joint = cpPivotJointNew(obj1->body, obj2->body ,cpv(pivot.x, pivot.y));
  cpSpaceAddJoint(space,joint); 
}

/***********************************************PhysicsObject**************************/

PhysicsObject::PhysicsObject(cpFloat mass, cpFloat inertia, Physics* physics, bool is_static):physics(physics), is_static(is_static) {
  body = cpBodyNew(mass, inertia);
  if (!is_static) {
    assert (physics->space != NULL);
    cpSpaceAddBody(physics->space, body);
  }
  shapes.clear();
}

PhysicsObject::PhysicsObject(cpBody* body, cpShape* shape):body(body), physics(0), is_static(false) {
  shapes.clear();
  if (shape != NULL)
    shapes.push_back(shape);
}

PhysicsObject::~PhysicsObject() {}

float PhysicsObject::GetAngle() {
  assert(body != NULL);
  return (float)body->a;
}

SexyVector2 PhysicsObject::GetRotation() {
  assert(body != NULL);
  return SexyVector2(body->rot.x, body->rot.y);
}

SexyVector2 PhysicsObject::GetPosition() {
  assert(body != NULL);
  return SexyVector2(body->p.x, body->p.y);
}

SexyVector2 PhysicsObject::GetVelocity() {
  assert(body != NULL);
  return SexyVector2(body->v.x, body->v.y);
}

void PhysicsObject::AddCircleShape(cpFloat radius, const SexyVector2& offset) {
  assert(body != NULL);
  cpShape* shape = cpCircleShapeNew(body, radius, cpv(offset.x, offset.y));
  assert(shape != NULL);
  if (physics->space != NULL) {
    if (is_static)
      cpSpaceAddStaticShape(physics->space, shape);
    else
      cpSpaceAddShape(physics->space, shape);
  }
 shapes.push_back(shape);
}

void PhysicsObject::AddSegmentShape(const SexyVector2& begin, const SexyVector2& end, cpFloat radius) {
  assert(body != NULL);
  cpShape* shape = cpSegmentShapeNew(body, cpv(begin.x, begin.y), cpv(end.x,end.y), radius);
  assert(shape != NULL);
 if (physics->space != NULL) {
    if (is_static)
      cpSpaceAddStaticShape(physics->space, shape);
    else
      cpSpaceAddShape(physics->space, shape);
  }
 shapes.push_back(shape);
}

void PhysicsObject::AddPolyShape(int numVerts, SexyVector2* vectors, const SexyVector2& offset) {
  assert(body != NULL);
  assert(sizeof(SexyVector2) == sizeof(cpVect));

  cpShape* shape = cpPolyShapeNew(body, numVerts, (cpVect*)vectors, cpv(offset.x, offset.y));
  assert(shape != NULL);
 if (physics->space != NULL) {
    if (is_static)
      cpSpaceAddStaticShape(physics->space, shape);
    else
      cpSpaceAddShape(physics->space, shape);
  }
 shapes.push_back(shape);
}

void PhysicsObject::SetAngularVelocity(cpFloat  w) { 
  assert(body != NULL); 
  body->w = w;
}

void PhysicsObject::SetVelocity(const SexyVector2& v) { 
  assert(body != NULL); 
  body->v = cpv(v.x, v.y);
}

void PhysicsObject::SetElasticity(cpFloat e, int shape_index) {
  assert(shapes.size() > shape_index);
  shapes[shape_index]->e = e;
}

void PhysicsObject::SetCollisionType(int type, int shape_index) {
  assert(shapes.size() > shape_index);
  shapes[shape_index]->collision_type = type;
}

int PhysicsObject::GetCollisionType(int shape_index) {
  assert(shapes.size() > shape_index);
  return shapes[shape_index]->collision_type;
}

void PhysicsObject::SetFriction(cpFloat u, int shape_index) {
  assert(shapes.size() > shape_index);
  shapes[shape_index]->u = u;
}

int PhysicsObject::GetShapeType(int shape_index) {
  assert(shapes.size() > shape_index);
  return shapes[shape_index]->type;
}

int PhysicsObject::GetNumberVertices(int shape_index) {
  assert(shapes.size() > shape_index && shapes[shape_index]->type ==(cpShapeType)POLY_SHAPE);
  return ((cpPolyShape*)shapes[shape_index])->numVerts;
}

SexyVector2 PhysicsObject::GetVertex(int vertex_index, int shape_index) {
  assert(shapes.size() > shape_index && shapes[shape_index]->type ==(cpShapeType)POLY_SHAPE);
  cpVect position = cpvadd(body->p, cpvrotate(((cpPolyShape*)shapes[shape_index])->verts[vertex_index], body->rot));
  return SexyVector2(position.x, position.y);
}

SexyVector2 PhysicsObject::GetSegmentShapeBegin(int shape_index) {
  assert(shapes.size() > shape_index && shapes[shape_index]->type ==(cpShapeType)SEGMENT_SHAPE);
  cpVect position = cpvadd(body->p, cpvrotate(((cpSegmentShape*)shapes[shape_index])->a, body->rot));
  return SexyVector2(position.x, position.y);  
}  

SexyVector2 PhysicsObject::GetSegmentShapeEnd(int shape_index) {
  assert(shapes.size() > shape_index && shapes[shape_index]->type ==(cpShapeType)SEGMENT_SHAPE);
  cpVect position = cpvadd(body->p, cpvrotate(((cpSegmentShape*)shapes[shape_index])->b, body->rot));
  return SexyVector2(position.x, position.y);  
}

float PhysicsObject::GetSegmentShapeRadius(int shape_index) {
  assert(shapes.size() > shape_index && shapes[shape_index]->type ==(cpShapeType)SEGMENT_SHAPE);
  return (float)((cpSegmentShape*)shapes[shape_index])->r;
}

float PhysicsObject::GetCircleShapeRadius(int shape_index) {
  assert(shapes.size() > shape_index && shapes[shape_index]->type ==(cpShapeType)CIRCLE_SHAPE);
  return (float)((cpCircleShape*)shapes[shape_index])->r;    
}

SexyVector2 PhysicsObject::GetCircleShapeCenter(int shape_index) {
  assert(shapes.size() > shape_index && shapes[shape_index]->type ==(cpShapeType)CIRCLE_SHAPE);
  cpVect position = cpvadd(body->p, cpvrotate(((cpCircleShape*)shapes[shape_index])->c, body->rot));     //FIXME does c stand for center of gravity??
  return SexyVector2(position.x, position.y);    
}

void PhysicsObject::UpdateVelocity() {
  assert(is_static && physics->space != NULL);
  cpBodyUpdateVelocity(body, physics->space->gravity, physics->space->damping, physics->delta);  
}

void PhysicsObject::UpdatePosition() {
  assert(is_static && physics->space != NULL);
  cpBodyUpdatePosition(body, physics->delta);
  cpSpaceRehashStatic(physics->space);
}
