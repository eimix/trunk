#include "PdmsTools.h"

//System
#include <iostream>
#include <stdlib.h>

using namespace PdmsTools;
using namespace PdmsCommands;
using namespace PdmsObjects;

/////////// MACROS ////////////

#define memalert(e, s)	std::cerr<<"Memory alert ["<<__FILE__<<", line "<<__LINE__<<"] with size "<<s<<" : "<<e.what()<<std::endl
#define memfail(e, s) memalert(e,s); abort()

/////////// FUNCTIONS ////////////
template<typename T> inline T sqr(const T &a) {return a*a;}

ElementCreation::ElementsStack* ElementCreation::s_elementsStack=NULL;
Token DistanceValue::workingUnit=PDMS_MILLIMETRE;
static GroupElement defaultWorld(PDMS_WORLD);

///////////////////////////////
// PDMS COMMANDS IMPLEMENTATION
///////////////////////////////

bool NumericalValue::handle(PointCoordinateType numvalue)
{
    value=numvalue;
    valueChanges++;
    return valueChanges==1;
}

bool NumericalValue::isValid() const
{
    return valueChanges<=1;
}

PointCoordinateType NumericalValue::getValue() const
{
    switch(command)
    {
        case PDMS_ANGLE:
        case PDMS_X_TOP_SHEAR:
        case PDMS_Y_TOP_SHEAR:
        case PDMS_X_BOTTOM_SHEAR:
        case PDMS_Y_BOTTOM_SHEAR:
            return (PointCoordinateType)(CC_DEG_TO_RAD) * value;
        default:
            return value;
    }
}

bool NumericalValue::execute(PdmsObjects::GenericItem **item) const
{
    if(!*item) return false;
    return((*item)->setValue(command, getValue()));
}



bool DistanceValue::handle(Token t)
{
    if(!PdmsToken::isUnit(t)) return false;
    if(!isValid()) return false;
    unit=t;
    return true;
}

PointCoordinateType DistanceValue::getValueInWorkingUnit() const
{
    if(unit==PDMS_MILLIMETRE && workingUnit==PDMS_METRE)
        return value/(PointCoordinateType)1000.;
    if(unit==PDMS_METRE && workingUnit==PDMS_MILLIMETRE)
        return value*(PointCoordinateType)1000.;
    return value;
}

bool DistanceValue::execute(PdmsObjects::GenericItem **item) const
{
    if(!*item) return false;
    return((*item)->setValue(command, getValueInWorkingUnit()));
}


Reference& Reference::operator=(const Reference &ref)
{
    command=ref.command;
    token=ref.token;
    strcpy(refname, ref.refname);
    return *this;
}

bool Reference::handle(Token t)
{
    if(isSet()) return false;
    //Todo : handle other references than grouping elements or design element
    if(!PdmsToken::isElement(t)) return false;
    token=t;
    return true;
}

bool Reference::handle(const char* str)
{
    if(isSet()) return false;
    strcpy(refname, str);
    return true;
}

bool Reference::isValid() const
{
	return ((command==PDMS_LAST && isSet()<=1) || (isSet()==1));
}

bool Reference::isNameReference() const
{
    return strlen(refname)>0;
}

bool Reference::isTokenReference() const
{
    return token != PDMS_INVALID_TOKEN;
}

int Reference::isSet() const
{
    int nb=0;
    if(strlen(refname)>0) nb++;
    if(token!=PDMS_INVALID_TOKEN) nb++;
    return nb;
}

bool Reference::execute(PdmsObjects::GenericItem **item) const
{
	if(!item) return false;

	//Handle the PDMS_LAST command
	if(command == PDMS_LAST)
	{
		ElementCreation::ElementsStack::const_reverse_iterator it;
		if(ElementCreation::s_elementsStack->size()<2)
			return false;
		it = ElementCreation::s_elementsStack->rbegin();
		it++;
		if(isSet()==1)
		{
			while(it!=ElementCreation::s_elementsStack->rend())
			{
				if(isNameReference() && strcmp(refname,(*it)->name)==0)
					break;
				if(isTokenReference() && (*it)->getType()==token)
					break;
				it++;
			}
			if(it==ElementCreation::s_elementsStack->rend())
				return false;
		}
		*item = *it;
		return true;
	}

	//Check that this reference is not a PDMS_OWNER termination command
    if(command == PDMS_OWNER && !isSet())
    {
        //Redirect to an ending command
        ElementEnding endCommand(PDMS_OWNER);
        return endCommand.execute(item);
    }

    GenericItem* result = NULL;
    //Search for the referenced item depending on the reference type
    if(isNameReference())
    {
        //Use the hierarchy scanning function given the requested object name
        result=(*item)->getRoot()->scan(refname);
    }
    //Request for an element (hierachical or design element only)
    else if(isTokenReference())
    {
        if(PdmsToken::isGroupElement(token))
        {
            //Go up in the hierarchy to find a matching, or the first group which can own the request item
            result = *item;
            while(result && result->getType()>token)
                result = result->owner;
            if(!result)
                result = &defaultWorld;
        }
        else if(PdmsToken::isDesignElement(token))
        {
            //Go up in the hierarchy untill we meet the requested type
            result = *item;
            while(result && result->getType()!=token)
                result = result->owner;
        }
        else
            return false;
    }

    //If the reference command is PDMS_OWNER, then we have to change the request item owner
    if(command==PDMS_OWNER && result)
    {
        (*item)->owner = result;
        result = (*item);
    }

    if(result) *item = result;

    return (result!=NULL);
}


bool Coordinates::handle(Token t)
{
    if(current>=3) return false;
    //Check that current activ comand cannot handle this token (if it cannot, check that it is valid before continuing)
    if(current>=0){if(coords[current].handle(t)) return true; if(!coords[current].isValid()) return false;}
    //Handle coordinates commands
    if(!PdmsToken::isCoordinate(t)) return false;
    current++;
    if(current>=3) return false;
    coords[current].command = t;
    //coords[current].value = 1.;
    coords[current].value = 0.;
    return true;
}

bool Coordinates::handle(PointCoordinateType numvalue)
{
    if(current<0 || current>=3) return false;
    //Only coordinates can handle numerical values
    if(!PdmsToken::isCoordinate(coords[current].command)) return false;
    return coords[current].handle(numvalue);
}

bool Coordinates::isValid() const
{
    //Could not be invalid
    return true;
}

bool Coordinates::getVector(CCVector3 &u) const
{
    bool ok[3]={false, false, false};
    int nb = getNbComponents();
    u = CCVector3((PointCoordinateType)0);
    for(int i=0; i<nb; i++)
    {
        if(!coords[i].isValid()) return false;
        if(ok[i]) return false;
        switch(coords[i].command)
        {
            case PDMS_X: case PDMS_EST: u[0]=coords[i].getValueInWorkingUnit(); ok[0]=true; break;
            case PDMS_WEST: u[0]=-coords[i].getValueInWorkingUnit(); ok[0]=true; break;
            case PDMS_Y: case PDMS_NORTH: u[1]=coords[i].getValueInWorkingUnit(); ok[1]=true; break;
            case PDMS_SOUTH: u[1]=-coords[i].getValueInWorkingUnit(); ok[1]=true; break;
            case PDMS_Z: case PDMS_UP: u[2]=coords[i].getValueInWorkingUnit(); ok[2]=true; break;
            case PDMS_DOWN: u[2]=-coords[i].getValueInWorkingUnit(); ok[2]=true; break;
            default: return false;
        }
    }
    return true;
}

int Coordinates::getNbComponents(bool onlyset) const
{
    int i, nb=0;
    for(i=0; i<3; i++)
    {
        if(PdmsToken::isCoordinate(coords[i].command))
        {
            if(!onlyset || (coords[nb].valueChanges>=1))
                nb++;
        }
    }
    return nb;
}



bool Position::handle(Token t)
{
    //Check if current activ command can handle this token (if it cannot, check that it is valid before continuing)
    if(current)
    {
        if(current->handle(t)) return true;
        if(!current->isValid()) return false;
    }
    //If no current command is activ, then token must be either a reference command or a coordinate ID
    //Handle PDMS_WRT (and check it has not been handled yet)
    if(t==PDMS_WRT)
    {
        current=&ref;
        if(current->command) return false;
        current->command=t;
        return true;
    }
    //Handle coordinates ID
    if(PdmsToken::isCoordinate(t))
    {
        current=&position;
        return current->handle(t);
    }
    //Not handled
    return false;
}

bool Position::handle(PointCoordinateType numvalue)
{
    if(!current) return false;
    return current->handle(numvalue);
}

bool Position::handle(const char* str)
{
    if(!current) return false;
    return current->handle(str);
}

bool Position::isValid() const
{
    if(!position.isValid()) return false;
    if(ref.command==PDMS_WRT) return ref.isValid();
    return true;
}

bool Position::execute(PdmsObjects::GenericItem **item) const
{
    if(!*item) return false;
    CCVector3 p;
    GenericItem *refpos=NULL;
    //Resolve reference if needed
    if(ref.isValid())
    {
        refpos=*item;
        if(!ref.execute(&refpos))
            return false;
    }
    //Get position point
    position.getVector(p);
    (*item)->setPosition(p);
    (*item)->positionReference = refpos;
    return true;
}


bool Orientation::handle(Token t)
{
    //Check that current activ comand cannot handle this token (if it cannot, check that it is valid before continuing)
    if(current)
    {
        if(current->handle(t)) return true;
        if(!current->isValid()) return false;
    }
    //PDMS_AND command exits current coordinates system
    if(t==PDMS_AND)
    {
        if(!(current && current->isValid())) return false;
        current = NULL;
        return true;
    }
    //PDMS_IS activates last specified component
    if(t==PDMS_IS)
    {
        if(component<0 || component>=3) return false;
        if(current) return false;
        current=&orientation[component];
        return true;
    }
    //Handle PDMS_WRT (and check it has not been handled yet)
    if(t==PDMS_WRT)
    {
        if(component<0 || component>=3) return false;
        current=&refs[component];
        if(current->command) return false;
        current->command=t;
        return true;
    }
    //Handle coordinates ID : here, we should create a new orientation axis (since no current element is activ)
    if(PdmsToken::isCoordinate(t))
    {
        component++;
        if(component>=3) return false;
        orientation[component].command=t;
        current = NULL;
        return true;
    }
    return false;
}

bool Orientation::handle(PointCoordinateType numvalue)
{
    if(!current) return false;
    return current->handle(numvalue);
}

bool Orientation::handle(const char* str)
{
    if(!current) return false;
    return current->handle(str);
}

bool Orientation::isValid() const
{
    int nb = getNbComponents();
    for(int i=0; i<nb; i++)
    {
        if(PdmsToken::isCoordinate(orientation[i].command)) return false;
        if(!orientation[i].isValid()) return false;
        if(refs[i].command==PDMS_WRT && !refs[i].isValid()) return false;
    }
    if(nb<=0) return false;
    return true;
}

bool Orientation::getAxis(CCVector3 &x, CCVector3 &y, CCVector3 &z) const
{
    int nb=getNbComponents();
    x = CCVector3((PointCoordinateType)0);
    y = CCVector3((PointCoordinateType)0);
    z = CCVector3((PointCoordinateType)0);
    for(int i=0; i<nb; i++)
    {
        if(!orientation[i].isValid()) return false;
        switch(orientation[i].command)
        {
            case PDMS_X: case PDMS_EST: if(!axisFromCoords(orientation[i],x)) return false; break;
            case PDMS_WEST: if(!axisFromCoords(orientation[i],x)) return false; x *= -1.; break;
            case PDMS_Y: case PDMS_NORTH: if(!axisFromCoords(orientation[i],y)) return false; break;
            case PDMS_SOUTH: if(!axisFromCoords(orientation[i],y)) return false; y *= -1.; break;
            case PDMS_Z: case PDMS_UP: if(!axisFromCoords(orientation[i],z)) return false; break;
            case PDMS_DOWN: if(!axisFromCoords(orientation[i],z)) return false; z *= -1.; break;
            default: return false;
        }
    }

    return nb!=0;
}

bool Orientation::axisFromCoords(const Coordinates &coords, CCVector3 &u)
{
    int nb=coords.getNbComponents(true);
	PointCoordinateType alpha, beta;
    if(!coords.getVector(u))
        return false;
    if(nb==2)
    {
        alpha = (PointCoordinateType)(CC_DEG_TO_RAD) * u[0];
        beta = (PointCoordinateType)(CC_DEG_TO_RAD) * u[1];
        u[0] = cos(alpha)*cos(beta);
        u[1] = sin(alpha)*cos(beta);
        u[2] = sin(beta);
    }
    return true;
}

int Orientation::getNbComponents() const
{
    int nb=0;
    while(nb<3 && orientation[nb].command)
        nb++;
    return nb;
}

bool Orientation::execute(PdmsObjects::GenericItem **item) const
{
    if(!item) return false;
    CCVector3 x, y, z;
    GenericItem *refori;
    //Resolve reference if needed
    for(unsigned i=0; i<3; i++)
    {
        refori = NULL;
        if(refs[i].isValid())
        {
            refori=*item;
            if(!refs[i].execute(&refori))
                return false;
        }
        (*item)->orientationReferences[i] = refori;
    }
    //Get position point
    if(!getAxis(x, y, z))
		return false;
    (*item)->setOrientation(x, y, z);
    return true;
}

bool Name::execute(PdmsObjects::GenericItem **item) const
{
    if(!*item) return false;
    strcpy((*item)->name, name);
    return true;
}


bool ElementCreation::handle(const char*str)
{
    if(!elementType) return false;
	if(path.size()>0) return false;
	return splitPath(str);
}

bool ElementCreation::handle(Token t)
{
    if(PdmsToken::isElement(t)){if(elementType) return false; elementType=t; return true;}
    return false;
}

bool ElementCreation::isValid() const
{
    return PdmsToken::isElement(elementType);
}

const char* ElementCreation::GetDefaultElementName(Token token)
{
    switch(token)
	{
        case PDMS_GROUP:
			return "Group";
        case PDMS_WORLD:
			return "World";
        case PDMS_SITE:
			return "Site";
        case PDMS_ZONE:
			return "Zone";
        case PDMS_EQUIPMENT:
			return "Equipment";
        case PDMS_STRUCTURE:
			return "Structure";
        case PDMS_SUBSTRUCTURE:
			return "Sub-structure";
        //PDMS elements
		case PDMS_SCYLINDER:
			return "Cylinder";
		case PDMS_CTORUS:
			return "Torus (C)";
		case PDMS_RTORUS:
			return "Torus (R)";
		case PDMS_DISH:
			return "Dish";
		case PDMS_CONE:
			return "Cone";
		case PDMS_BOX:
			return "Box";
		case PDMS_NBOX:
			return "Box(-)";
		case PDMS_PYRAMID:
			return "Pyramid";
		case PDMS_SNOUT:
			return "Snout";
		case PDMS_EXTRU:
			return "Extrusion";
		case PDMS_NEXTRU:
			return "Extrusion(-)";
		case PDMS_LOOP:
			return "Loop";
		case PDMS_VERTEX:
			return "Vertex";
    }

	return 0;
}

bool ElementCreation::execute(PdmsObjects::GenericItem **item) const
{
    GenericItem* newElement=NULL;
    switch(elementType)
	{
        case PDMS_GROUP:
        case PDMS_WORLD:
        case PDMS_SITE:
        case PDMS_ZONE:
        case PDMS_EQUIPMENT:
        case PDMS_STRUCTURE:
        case PDMS_SUBSTRUCTURE:
			try{ newElement = new GroupElement(elementType); }
			catch(std::exception &nex) {memalert(nex,1); return false;}
			break;
        //PDMS elements
		case PDMS_SCYLINDER:
			try{ newElement = new SCylinder; }
			catch(std::exception &nex) {memalert(nex,1); return false;}
			break;
		case PDMS_CTORUS:
			try{ newElement = new CTorus; }
			catch(std::exception &nex) {memalert(nex,1); return false;}
			break;
		case PDMS_RTORUS:
			try{ newElement = new RTorus; }
			catch(std::exception &nex) {memalert(nex,1); return false;}
			break;
		case PDMS_DISH:
			try{ newElement = new Dish; }
			catch(std::exception &nex) {memalert(nex,1); return false;}
			break;
		case PDMS_CONE:
			try{ newElement = new Cone; }
			catch(std::exception &nex) {memalert(nex,1); return false;}
			break;
		case PDMS_BOX:
		case PDMS_NBOX:
			try{ newElement = new Box; }
			catch(std::exception &nex) {memalert(nex,1); return false;}
			static_cast<Box*>(newElement)->negative=(elementType==PDMS_NBOX);
			break;
		case PDMS_PYRAMID:
			try{ newElement = new Pyramid; }
			catch(std::exception &nex) {memalert(nex,1); return false;}
			break;
		case PDMS_SNOUT:
			try{ newElement = new Snout; }
			catch(std::exception &nex) {memalert(nex,1); return false;}
			break;
		case PDMS_EXTRU:
		case PDMS_NEXTRU:
			try{ newElement = new Extrusion; }
			catch(std::exception &nex) {memalert(nex,1); return false;}
			static_cast<Extrusion*>(newElement)->negative=(elementType==PDMS_NEXTRU);
			break;
		case PDMS_LOOP:
			try{ newElement = new Loop; }
			catch(std::exception &nex) {memalert(nex,1); return false;}
			break;
		case PDMS_VERTEX:
			try{ newElement = new Vertex; }
			catch(std::exception &nex) {memalert(nex,1); return false;}
			break;
        default:
            break;
    }

    if(!newElement)
		return false;

	const char* name = GetDefaultElementName(elementType);
	if (name)
		strcpy(newElement->name,name);

	//If the path is changed during the creation, do it now
	if(path.size()>1)
	{
		PdmsObjects::GenericItem *mitem = *item;
		if(!mitem) {delete newElement; return false;}
		mitem = mitem->getRoot();
		for(unsigned i=0; i<path.size()-1; i++)
		{
			mitem = mitem->scan(path[i].c_str());
			if(!mitem)
			{
				delete newElement;
				return false;
			}
		}
		*item = mitem;
	}
	//Then we can push the new element in the hierarchy
	if((*item) && !((*item)->push(newElement)))
	{
		delete newElement;
		return false;
	}
	newElement->creator = newElement->owner;
	if(path.size())
		strcpy(newElement->name, path.back().c_str());
	*item=newElement;
	try
	{
		s_elementsStack->push_back(newElement);
	}
	catch(std::exception &pex)
	{
		memalert(pex,1);
		return false;
	}
	return true;
}

bool ElementCreation::Initialize()
{
	if(s_elementsStack)
		delete s_elementsStack;
	try
	{
		s_elementsStack = new std::list<PdmsObjects::GenericItem*>;
	}
	catch(std::exception &nex)
	{
		memalert(nex,1);
		return false;
	}

	return true;
}

void ElementCreation::Finalize()
{
	if(s_elementsStack)
		delete s_elementsStack;
	s_elementsStack = NULL;
}

bool ElementEnding::execute(PdmsObjects::GenericItem **item) const
{
    if(!*item)
		return false;

	GenericItem* result=NULL;
    switch(command)
    {
        case PDMS_OWNER:
            //If the ending command is PDMS_OWNER, then we simply go back to the item owner
            result=(*item)->owner;
            break;
        case PDMS_END:
            //Bug realworks : ignore END EXTRU command
			if(end.isTokenReference() && (end.token==PDMS_EXTRU || end.token==PDMS_NEXTRU))
				return true;
            //If the general case, we have to find the references item (default : this one), and go back to its creator
            result = *item;
            if(end.isValid())
            {
                if(!end.execute(&result))
                    return false;
            }
            if(result)
				result=result->creator;
            else
				return false;
            break;
		case PDMS_LAST:
			if(!end.execute(&result))
				return false;
			break;
        default:
			return false;
    }

    *item = result;

    return true;
}

bool ElementCreation::splitPath(const char *str)
{
	path.clear();

	//Each time a new '/' is met, we create a new entry in the path
	unsigned i=0;
	while(str[i])
	{
		if(str[i] == '/')
		{
			if(i>0)
				path.push_back(std::string(str, i));
			str = &str[i+1];
			i = 0;
		}
		else
			i++;
	}

	//At the end, we have to create an entry for the last word
	if(i>0)
		path.push_back(std::string(str, i));
	return (path.size() != 0);
}


bool HierarchyNavigation::execute(PdmsObjects::GenericItem **item) const
{
    GenericItem *result=*item;
    if(!result || !isValid())
		return true;

	//Go back to the first creator object that matches or can contain the command hierarchy level
    while(result && command<result->getType())
        result = result->creator;

	//If we went to the root, we have to create a new hierarchy level and set it as the new root
    if(!result)
    {
		try
		{
			result = new GroupElement(command);
		}
		catch(std::exception &nex)
		{
			memfail(nex,1);
			return false;
		}
        result->push(*item);
    }

	//change the current element as the new accessed element
    *item=result;
    return true;
}


Command* Command::Create(Token t)
{
	Command *result = NULL;
    switch(t)
    {
		case PDMS_CREATE:
			try{ result = new ElementCreation; }
			catch(std::exception &nex) {memfail(nex,1);}
			break;
		case PDMS_END:
		case PDMS_LAST:
			try{ result = new ElementEnding(t); }
			catch(std::exception &nex) {memfail(nex,1);}
			break;
		case PDMS_WRT:
		case PDMS_OWNER:
			try{ result = new Reference(t); }
			catch(std::exception &nex) {memfail(nex,1);}
			break;
		case PDMS_NAME:
			try{ result = new Name; }
			catch(std::exception &nex) {memfail(nex,1);}
			break;
        //Attributes
        case PDMS_DIAMETER:
        case PDMS_HEIGHT:
        case PDMS_RADIUS:
        case PDMS_INSIDE_RADIUS:
        case PDMS_OUTSIDE_RADIUS:
        case PDMS_TOP_DIAMETER:
        case PDMS_BOTTOM_DIAMETER:
        case PDMS_XLENGTH:
        case PDMS_YLENGTH:
        case PDMS_ZLENGTH:
        case PDMS_X_OFF:
        case PDMS_Y_OFF:
        case PDMS_X_BOTTOM:
        case PDMS_Y_BOTTOM:
        case PDMS_X_TOP:
        case PDMS_Y_TOP:
			try{ result = new DistanceValue(t); }
			catch(std::exception &nex) {memfail(nex,1);}
			break;
        case PDMS_X_TOP_SHEAR:
        case PDMS_X_BOTTOM_SHEAR:
        case PDMS_Y_TOP_SHEAR:
        case PDMS_Y_BOTTOM_SHEAR:
        case PDMS_ANGLE:
			try{ result = new NumericalValue(t); }
			catch(std::exception &nex) {memfail(nex,1);}
			break;
        case PDMS_POSITION:
			try{ result = new Position; }
			catch(std::exception &nex) {memfail(nex,1);}
			break;
        case PDMS_ORIENTATION:
			try{ result = new Orientation; }
			catch(std::exception &nex) {memfail(nex,1);}
			break;
        case PDMS_WORLD:
        case PDMS_SITE:
        case PDMS_ZONE:
        case PDMS_EQUIPMENT:
        case PDMS_STRUCTURE:
        case PDMS_SUBSTRUCTURE:
			try{ result = new HierarchyNavigation(t); }
			catch(std::exception &nex) {memfail(nex,1);}
			break;
        default: break;
    }

	return result;
}



///////////////////////////////
// PDMS OBJECTS IMPLEMENTATION
///////////////////////////////

GenericItem::GenericItem()
	: owner(NULL)
	, creator(NULL)
	, position((PointCoordinateType)0)
	, isCoordinateSystemUpToDate(false)
	, positionReference(NULL)
{
    orientationReferences[0]=orientationReferences[1]=orientationReferences[2]=NULL;
    orientation[0] = CCVector3((PointCoordinateType)0); orientation[0][0] = (PointCoordinateType)1;
    orientation[1] = CCVector3((PointCoordinateType)0); orientation[1][1] = (PointCoordinateType)1;
    orientation[2] = CCVector3((PointCoordinateType)0); orientation[2][2] = (PointCoordinateType)1;
    name[0] = '\0';
}

bool GenericItem::setPosition(const CCVector3 &p)
{
	position = p;
    return true;
}

bool GenericItem::setOrientation(const CCVector3 &x, const CCVector3 &y, const CCVector3 &z)
{
	orientation[0] = x;
	orientation[1] = y;
	orientation[2] = z;
	return true;
}

bool GenericItem::isOrientationValid(unsigned i) const
{
    return (orientation[i].norm2() > ZERO_TOLERANCE);
}

bool GenericItem::completeOrientation()
{
    bool ok[3] = {true, true, true};
    unsigned i, nb;

    for(i=0, nb=0; i<3; i++)
    {
        if(isOrientationValid(i))
            nb++;
        else
            ok[i]=false;
    }

    switch(nb)
    {
        case 1:
            if(ok[0]) {orientation[0].normalize(); orientation[1] = orientation[0].orthogonal(); orientation[2] = orientation[0].cross(orientation[1]);}
            if(ok[1]) {orientation[1].normalize(); orientation[2] = orientation[1].orthogonal(); orientation[0] = orientation[1].cross(orientation[2]);}
            if(ok[2]) {orientation[2].normalize(); orientation[0] = orientation[2].orthogonal(); orientation[1] = orientation[2].cross(orientation[0]);}
            break;
        case 2:
            if(!ok[0]) {orientation[1].normalize(); orientation[2].normalize(); orientation[0] = orientation[1].cross(orientation[2]);}
            if(!ok[1]) {orientation[0].normalize(); orientation[2].normalize(); orientation[1] = orientation[2].cross(orientation[0]);}
            if(!ok[2]) {orientation[0].normalize(); orientation[1].normalize(); orientation[2] = orientation[0].cross(orientation[1]);}
            break;
        case 3:
            break;
        default: return false;;
    }

    return true;
}

bool GenericItem::convertCoordinateSystem()
{
    unsigned i, j, k;
    GenericItem *ref = NULL;
    CCVector3 axis[3], p;

    if(isCoordinateSystemUpToDate) return true;
    if(!positionReference) positionReference = owner;
    for(k=0; k<3; k++)
        if(!orientationReferences[k]) orientationReferences[k] = owner;
    //Update position coordinates
    if(positionReference)
    {
        if(!positionReference->convertCoordinateSystem()) return false;
        //New position is reference origin plus sum(reference_axis[i]*point_coordinate[i])
        ref = positionReference;
		p = position;
        if(!ref->isCoordinateSystemUpToDate && ref->owner==this) return false;
        for(i=0; i<3; i++)
            position[i] = ref->orientation[0][i]*p[0]+ref->orientation[1][i]*p[1]+ref->orientation[2][i]*p[2];
        position += ref->position;
    }
    //The same for orientation
    for(k=0; k<3; k++)
    {
        if(!isOrientationValid(k))
            continue;
        if(orientationReferences[k])
        {
            if(!orientationReferences[k]->convertCoordinateSystem()) return false;
        	//New axis is sum(reference_axis[i]*axis[i])
	        ref = orientationReferences[k];
	        if(!ref->isCoordinateSystemUpToDate && ref->owner==this) return false;
	        for(j=0; j<3; j++)
	            axis[j] = orientation[j];
	        for(j=0; j<3; j++)
	            for(i=0; i<3; i++)
	                orientation[j][i] = ref->orientation[0][i]*axis[j][0]+ref->orientation[1][i]*axis[j][1]+ref->orientation[2][i]*axis[j][2];
		}
    }
    if(!completeOrientation())
    	return false;
    isCoordinateSystemUpToDate = true;
    return true;
}

bool GenericItem::scan(Token t, std::vector<GenericItem *> &array)
{
	if(getType()==t)
	{
		try{ array.push_back(this); }
		catch(std::exception &pex) {memfail(pex,array.size());}
		return true;
	}
	return false;
}

DesignElement::~DesignElement()
{
	std::list<DesignElement*>::iterator it;
	for(it=nelements.begin(); it!=nelements.end(); it++)
		delete *it;
}

bool DesignElement::push(GenericItem *i)
{
	if(i->isDesignElement())
	{
		DesignElement *element=static_cast<DesignElement*>(i);
		if(element->negative)
		{
			try{nelements.push_back(element);}
			catch(std::exception &pex) {memalert(pex,nelements.size()); return false;}
			if(element->owner)
				element->owner->remove(element);
			element->owner = this;
			return true;
		}
	}
    //In most cases, design elements do not handle nested elements
    if(owner) return owner->push(i);
    return false;
}

void DesignElement::remove(GenericItem *i)
{
	std::list<DesignElement*>::iterator it;
	for(it=nelements.begin(); it!= nelements.end();)
	{
		if(*it == i)
			nelements.erase(it);
		else
			it++;
	}
}

//Model* DesignElement::toModel(unsigned filter)
//{
//	Model *model=NULL;
//	Shape *shape = toShape();
//	if(!shape)
//		return NULL;
//	if(Shape::testMask(filter, shape->getType()))
//	{
//		try{model = new Model;}
//		catch(std::exception &nex) {memalert(nex,1); delete shape; return NULL;}
//		if(!model->pushElement(shape))
//		{
//			delete model;
//			return NULL;
//		}
//	}
//	else
//		delete shape;
//	return model;
//}


GroupElement::GroupElement(Token l)
{
    level = l;
    elements.clear();
    subhierarchy.clear();
    memset(name,0,c_max_str_length);
}

//GroupElement::GroupElement(const Model* model)
//{
//	assert(model);
//	unsigned i;
//	DesignElement *element;
//
//	level = PDMS_GROUP;
//	memset(name,0,c_max_str_length);
//	if(model->getName())
//		strcpy(name, model->getName());
//	for(i=0; i<model->nbElements(); i++)
//	{
//		element = NULL;
//		switch(model->getElement(i).getType())
//		{
//			case Shape::CYLINDER:
//				if(model->getElement(i).isBounded())
//				{
//					try{element = new SCylinder(static_cast<const BCylinder&>(model->getElement(i)));}
//					catch(std::exception &nex) {memfail(nex,1);}
//				}
//				else
//					std::cerr << "Cannot create a PDMS cylinder from unbouded shape" << std::endl;
//				break;
//			case Shape::PLANE:
//				if(model->getElement(i).isBounded())
//				{
//					try{element = new Box(static_cast<const BPlane&>(model->getElement(i)),0);}
//					catch(std::exception &nex) {memfail(nex,1);}
//				}
//				else
//					std::cerr << "Cannot create a PDMS box from unbounded plane" << std::endl;
//				break;
//			case Shape::CTORUS:
//				element = new CTorus(static_cast<const Torus&>(model->getElement(i)));
//				break;
//			default:
//				std::cerr << "Cannot handle shape type " << model->getElement(i).getType() << std::endl;
//				break;
//		}
//
//		if(element)
//		{
//			try{elements.push_back(element);}
//			catch(std::exception &pex) {memfail(pex, elements.size());}
//		}
//	}
//
//	for(i=0; i<model->nbSubModels(); i++)
//	{
//		try{subhierarchy.push_back(new GroupElement(model->getSubModel(i)));}
//		catch(std::exception &pex) {memfail(pex, subhierarchy.size());}
//	}
//}

GroupElement::~GroupElement()
{
	clear(true);
}

void GroupElement::clear(bool del)
{
	if(del)
	{
		std::list<GroupElement*>::iterator hit;
		std::list<DesignElement*>::iterator eit;
		for(eit=elements.begin(); eit!=elements.end(); eit++)
			delete *eit;
		for(hit=subhierarchy.begin(); hit!=subhierarchy.end(); hit++)
			delete *hit;
	}
	elements.clear();
	subhierarchy.clear();
}

bool GroupElement::push(GenericItem *i)
{
    //In each case, the insertion of a new element consists in finding the list in which it should be added

    //If the request item is a group, we have to find its new place in the hierarchy
    if(PdmsToken::isGroupElement(i->getType()))
    {
        //If this group can contain the request item, then insert it in the group list
        GroupElement *group = dynamic_cast<GroupElement*>(i);
        if(group->level==PDMS_GROUP || group->level>level)
        {
            if(group->owner) group->owner->remove(group);
            group->owner=this;
			try{ subhierarchy.push_back(group); }
			catch(std::exception &pex) {memalert(pex,subhierarchy.size()); return false;}
        }
        //else the requested item should be inserted in this group owner
        else if(owner)
            owner->push(group);
        else
            return false;
    }
    //For design elements, insert it in the group' design element list
    else if(PdmsToken::isDesignElement(i->getType()))
    {
        if(i->owner) i->owner->remove(i);
		i->owner = this;
		try{  elements.push_back(dynamic_cast<DesignElement*>(i)); }
		catch(std::exception &pex) {memalert(pex,elements.size()); return false;}
        return true;
    }
    return true;
}

//bool GroupElement::push(const Shape* shape)
//{
//	assert(shape);
//
//	DesignElement *element=NULL;
//	switch(shape->getType())
//	{
//		case Shape::CYLINDER:
//			if(shape->isBounded())
//				element = new SCylinder(static_cast<const BCylinder&>(shape));
//			else
//				return false;
//			break;
//		case Shape::PLANE:
//			if(shape->isBounded())
//				element = new Box(static_cast<const BPlane&>(shape));
//			else
//				return false;
//			break;
//		default:
//			return false;
//	}
//
//	if(!push(element))
//	{
//		delete element;
//		return false;
//	}
//	return true;
//}

void GroupElement::remove(GenericItem *i)
{
	std::list<GroupElement*>::iterator hit;
    for(hit=subhierarchy.begin(); hit!=subhierarchy.end(); hit++) {if(*hit==i) {subhierarchy.erase(hit); return;}}

    std::list<DesignElement*>::iterator eit;
    for(eit=elements.begin(); eit!=elements.end(); eit++) {if(*eit==i) {elements.erase(eit); return;}}
}


bool GroupElement::convertCoordinateSystem()
{
    std::list<GroupElement*>::iterator hit;
    std::list<DesignElement*>::iterator eit;

    //Important : check that the object is not up to date, to avoid infinite loops
    if(isCoordinateSystemUpToDate)
		return true;

    if(!GenericItem::convertCoordinateSystem())
		return false;
	for(eit=elements.begin(); eit!=elements.end(); eit++)
		if(!(*eit)->convertCoordinateSystem())
			return false;
	for(hit=subhierarchy.begin(); hit!=subhierarchy.end(); hit++)
		if(!(*hit)->convertCoordinateSystem())
			return false;
	return true;
}

GenericItem* GroupElement::scan(const char* str)
{
    std::list<GroupElement*>::iterator hit;
    std::list<DesignElement*>::iterator eit;

    //scan all elements contained in this group, begining with this one, while none matches the requested name
    GenericItem *item=GenericItem::scan(str);
    for(eit=elements.begin(); eit!=elements.end() && !item; eit++)
        item=(*eit)->scan(str);
    for(hit=subhierarchy.begin(); hit!=subhierarchy.end() && !item; hit++)
        item=(*hit)->scan(str);
    return item;
}

bool GroupElement::scan(Token t, std::vector<GenericItem*> &items)
{
    std::list<GroupElement*>::iterator hit;
    std::list<DesignElement*>::iterator eit;

    GenericItem::scan(t, items);
	unsigned size = items.size();
	for(eit=elements.begin(); eit!=elements.end(); eit++)
		(*eit)->scan(t, items);
	for(hit=subhierarchy.begin(); hit!=subhierarchy.end(); hit++)
		(*hit)->scan(t, items);
	return (items.size() > size);
}

std::pair<int,int> GroupElement::write(std::ostream &output, int nbtabs) const
{
	std::pair<int,int> nb(0,0), n;

	{
		for(int i=0; i<nbtabs; i++)
			output << "\t";
	}

	output << "NEW ";
	switch(level)
	{
		case PDMS_GROUP: output << "GROUP"; break;
		case PDMS_WORLD: output << "WORLD"; break;
		case PDMS_SITE: output << "SITE"; break;
		case PDMS_ZONE: output << "ZONE"; break;
		case PDMS_EQUIPMENT: output << "EQUIPMENT"; break;
		case PDMS_STRUCTURE: output << "STRUCTURE"; break;
		case PDMS_SUBSTRUCTURE: output << "SUBSTRUCTURE"; break;
		default : std::cout << "Error : cannot write group " << level << std::endl; return nb;
	}

	if(strlen(name))
		output << " /" << name;
	output << std::endl;

	nb.first += 1;

    std::list<GroupElement*>::const_iterator hit;
    for(hit=subhierarchy.begin(); hit!=subhierarchy.end(); hit++)
	{
		n = (*hit)->write(output, nbtabs+1);
		nb.first += n.first;
		nb.second += n.second;
	}

    std::list<DesignElement*>::const_iterator eit;
    for(eit=elements.begin(); eit!=elements.end(); eit++)
	{
		n = (*eit)->write(output, nbtabs+1);
		nb.first += n.first;
		nb.second += n.second;
	}

	{
		for(int i=0; i<nbtabs; i++)
			output << "\t";
	}
	output << "END" << std::endl;
	return nb;
}

//Model* GroupElement::toModel(unsigned filter)
//{
//	Model *result=NULL, *submodel=0;
//	std::list<GroupElement*>::const_iterator hit;
//	std::list<DesignElement*>::const_iterator eit;
//	TODO FIXME
//	Token token;
//	unsigned i, k;
//
//	result = new Model;
//	result->setName(name);
//
//	for(eit=elements.begin(); eit!=elements.end(); eit++)
//	{
//		token = (*eit)->getType();
//		if(token==PDMS_SCYLINDER /*&& Shape::testMask(filter,Shape::CYLINDER)*/)
//		{
//			if(!result->pushElement(static_cast<SCylinder*>(*eit)->toShape()))
//			{
//				delete result;
//				return NULL;
//			}
//		}
//		else if(token==PDMS_CTORUS && Shape::testMask(filter,Shape::CTORUS))
//		{
//			if(!result->pushElement(static_cast<CTorus*>(*eit)->toShape()))
//			{
//				delete result;
//				return NULL;
//			}
//		}
//		else if(token==PDMS_BOX && Shape::testMask(filter,Shape::PLANE))
//		{
//			//Check the box is not a plane
//			for(i=0, k=4; i<3; i++)
//				if(static_cast<Box*>(*eit)->lengths[i]<=ZERO_TOLERANCE)
//					k = i;
//			if(k>3)
//			{
//				for(i=0; i<6; i++)
//				{
//					if(!result->pushElement(static_cast<Box*>(*eit)->getPlane(i)))
//					{
//						delete result;
//						return NULL;
//					}
//				}
//			}
//			else
//			{
//				if(!result->pushElement(static_cast<Box*>(*eit)->getPlane(2*k)))
//				{
//					delete result;
//					return NULL;
//				}
//			}
//		}
//	}
//
//	for(hit=subhierarchy.begin(); hit!=subhierarchy.end(); hit++)
//	{
//		submodel = (*hit)->toModel(filter);
//		if(!(submodel && result->pushSubModel(submodel)))
//		{
//			delete result;
//			return NULL;
//		}
//	}
//	return result;
//}


bool SCylinder::setValue(Token t, PointCoordinateType value)
{
    switch(t)
    {
        case PDMS_DIAMETER: diameter = value; break;
        case PDMS_HEIGHT: height=value; break;
        case PDMS_X_TOP_SHEAR: xtshear=value; if(fabsf(xtshear)>90.) return false; break;
        case PDMS_Y_TOP_SHEAR: ytshear=value; if(fabsf(ytshear)>90.) return false; break;
        case PDMS_X_BOTTOM_SHEAR: xbshear=value; if(fabsf(xbshear)>90.) return false; break;
        case PDMS_Y_BOTTOM_SHEAR: ybshear=value; if(fabsf(ybshear)>90.) return false; break;
        default: return false;
    }
    return true;
}


//Shape* SCylinder::toShape()
//{
//	CCVector3 u, v;
//    BCylinder *cylinder = NULL;
//
//    if(!convertCoordinateSystem())
//        return NULL;
//
//    if(diameter<=0. || height <= 0.)
//        return NULL;
//
//	try{ cylinder = new BCylinder(); }
//	catch(std::exception &nex) {memfail(nex,1);}
//    cylinder->setRadius(diameter/2.);
//    cylinder->setHeight(height);
//    cylinder->setPosition(position);
//    cylinder->setAxis(orientation[Shape::X_AXIS], Shape::X_AXIS, orientation[Shape::Y_AXIS], Shape::Y_AXIS);
//    u = (PointCoordinateType)cos(xtshear)*orientation[0] + (PointCoordinateType)sin(xtshear)*orientation[2];
//    v = (PointCoordinateType)cos(ytshear)*orientation[1] + (PointCoordinateType)sin(ytshear)*orientation[2];
//    cylinder->setUpperBound(u _CROSS_ v);
//    u = (PointCoordinateType)cos(xbshear)*orientation[0] + (PointCoordinateType)sin(xbshear)*orientation[2];
//    v = (PointCoordinateType)cos(ybshear)*orientation[1] + (PointCoordinateType)sin(ybshear)*orientation[2];
//	cylinder->setLowerBound((PointCoordinateType)-1.*(u _CROSS_ v));
//	cylinder->setName(name);
//    return cylinder;
//}

//void SCylinder::fromShape(const BCylinder &cyl)
//{
//	CCVector3 dir;
//	diameter = cyl.getRadius()*2.;
//	height = cyl.getHeight();
//	orientation[Shape::X_AXIS] = cyl.getAxis(Shape::X_AXIS);
//	orientation[Shape::Y_AXIS] = cyl.getAxis(Shape::Y_AXIS);
//	orientation[Shape::Z_AXIS] = cyl.getAxis(Shape::Z_AXIS);
//	position = cyl.getPosition();
//	dir = cyl.getUpperBound(true).getNormal();
//	if(dir[2]<0.) dir *= -1.;
//    xtshear = acos(dir[0]/sqrt(sqr(dir[0])+sqr(dir[2])))-M_PI/2.;
//    ytshear = acos(dir[1]/sqrt(sqr(dir[1])+sqr(dir[2])))-M_PI/2.;
//    dir = cyl.getLowerBound(true).getNormal();
//	if(dir[2]<0.) dir *= -1.;
//	xbshear = acos(dir[0]/sqrt(sqr(dir[0])+sqr(dir[2])))-M_PI/2.;
//	ybshear = acos(dir[1]/sqrt(sqr(dir[1])+sqr(dir[2])))-M_PI/2.;
//	if(cyl.getName())
//		strcpy(name, cyl.getName());
//}

PointCoordinateType SCylinder::surface() const
{
	return (PointCoordinateType)M_PI*diameter*height;
}

std::pair<int, int> SCylinder::write(std::ostream &output, int nbtabs) const
{
	int i;
	for(i=0; i<nbtabs; i++)
		output << "\t";
	output << "NEW SLCYLINDER";
	if(strlen(name))
		output << " /" << name;
	output << std::endl;

	for(i=0; i<=nbtabs; i++) output << "\t";
	output << "DIAMETER " << diameter << std::endl;
	for(i=0; i<=nbtabs; i++) output << "\t";
	output << "HEIGHT " << height << std::endl;
	for(i=0; i<=nbtabs; i++) output << "\t";
	output << "XTSHEAR " << (CC_RAD_TO_DEG)*xtshear << std::endl;
	for(i=0; i<=nbtabs; i++) output << "\t";
	output << "XBSHEAR " << (CC_RAD_TO_DEG)*xbshear << std::endl;
	for(i=0; i<=nbtabs; i++) output << "\t";
	output << "YTSHEAR " << (CC_RAD_TO_DEG)*ytshear << std::endl;
	for(i=0; i<=nbtabs; i++) output << "\t";
	output << "YBSHEAR " << (CC_RAD_TO_DEG)*ybshear << std::endl;
	for(i=0; i<=nbtabs; i++) output << "\t";
	output << "AT X " << position[0] << " Y " << position[1] << " Z " << position[2] << std::endl;
	for(i=0; i<=nbtabs; i++) output << "\t";
	output << "ORI ";
	output << "X is X " << orientation[0][0] << " Y " << orientation[0][1] << " Z " << orientation[0][2];
	output << " AND Z is X " << orientation[2][0] << " Y " << orientation[2][1] << " Z " << orientation[2][2] << std::endl;

	for(i=0; i<nbtabs; i++) output << "\t";
	output << "END" << std::endl;
	return std::pair<int,int>(0,1);
}

bool CTorus::setValue(Token t, PointCoordinateType value)
{
    switch(t)
    {
        case PDMS_ANGLE: angle=value; if(fabsf(angle)>(2.*M_PI)) return false; break;
        case PDMS_INSIDE_RADIUS: inside_radius=value; break;
        case PDMS_OUTSIDE_RADIUS: outside_radius=value; break;
        default: return false;
    }
    return true;
}


//Shape* CTorus::toShape()
//{
//	PointCoordinateType minorr, majorr;
//
//	majorr = (PointCoordinateType)0.5*(inside_radius+outside_radius);
//	minorr = (PointCoordinateType)0.5*(outside_radius-inside_radius);
//	Torus *t = new Torus(position, orientation[2], orientation[0], majorr, minorr, angle);
//	t->setName(name);
//	return t;
//}

//void CTorus::fromShape(const Torus* tor)
//{
//	assert(tor);
//	orientation[Shape::X_AXIS] = tor->getAxis(Shape::X_AXIS);
//	orientation[Shape::Y_AXIS] = tor->getAxis(Shape::Y_AXIS);
//	orientation[Shape::Z_AXIS] = tor->getAxis(Shape::Z_AXIS);
//	position = tor->getPosition();
//	inside_radius = tor->getMajorRadius()-tor.getMinorRadius();
//	outside_radius = tor->getMajorRadius()+tor->getMinorRadius();
//	angle = tor->getOpenAngle();
//	if(tor->getName())
//		strcpy(name, tor->getName());
//}

PointCoordinateType CTorus::surface() const
{
	PointCoordinateType r, R;
	r = (PointCoordinateType)0.5*(outside_radius-inside_radius);
	R = outside_radius-r;
	return (angle/PointCoordinateType(2.0*M_PI))*(PointCoordinateType(4.0*sqr(M_PI))*r*R);
}

std::pair<int,int> CTorus::write(std::ostream &output, int nbtabs) const
{
	int i;

	for(i=0; i<nbtabs; i++) output << "\t";
	output << "NEW CTORUS";
	if(strlen(name))
		output << " /" << name;
	output << std::endl;

	for(i=0; i<=nbtabs; i++) output << "\t";
	output << "RINSIDE " << inside_radius << std::endl;
	for(i=0; i<=nbtabs; i++) output << "\t";
	output << "ROUTSIDE " << outside_radius << std::endl;
	for(i=0; i<=nbtabs; i++) output << "\t";
	output << "ANGLE " << (PointCoordinateType)(CC_RAD_TO_DEG)*angle << std::endl;
	for(i=0; i<=nbtabs; i++) output << "\t";
	output << "AT X " << position[0] << " Y " << position[1] << " Z " << position[2] << std::endl;
	for(i=0; i<=nbtabs; i++) output << "\t";
	output << "ORI ";
	output << "X is X " << orientation[0][0] << " Y " << orientation[0][1] << " Z " << orientation[0][2];
	output << " AND Z is X " << orientation[2][0] << " Y " << orientation[2][1] << " Z " << orientation[2][2] << std::endl;

	for(i=0; i<nbtabs; i++) output << "\t";
	output << "END" << std::endl;
	return std::pair<int,int>(0,1);
}

bool RTorus::setValue(Token t, PointCoordinateType value)
{
    switch(t)
    {
        case PDMS_ANGLE: angle=value; if(fabsf(angle)>(2.*M_PI)) return false; break;
        case PDMS_INSIDE_RADIUS: inside_radius=value; break;
        case PDMS_OUTSIDE_RADIUS: outside_radius=value; break;
        case PDMS_HEIGHT: height=value; break;
        default: return false;
    }
    return true;
}

PointCoordinateType RTorus::surface() const
{
	PointCoordinateType inside, outside, updown;
	inside = (PointCoordinateType)(2.0*M_PI)*inside_radius*height;
	outside = (PointCoordinateType)(2.0*M_PI)*outside_radius*height;
	updown = (PointCoordinateType)(2.0*M_PI)*(sqr(outside_radius)-sqr(inside_radius));
	return (angle/(PointCoordinateType)(2.0*M_PI))*(inside+outside+updown);
}

std::pair<int,int> RTorus::write(std::ostream &output, int nbtabs) const
{
	int i;

	for(i=0; i<nbtabs; i++) output << "\t";
	output << "NEW RTORUS";
	if(strlen(name))
		output << " /" << name;
	output << std::endl;

	for(i=0; i<=nbtabs; i++) output << "\t";
	output << "RINSIDE " << inside_radius << std::endl;
	for(i=0; i<=nbtabs; i++) output << "\t";
	output << "ROUTSIDE " << outside_radius << std::endl;
	for(i=0; i<=nbtabs; i++) output << "\t";
	output << "HEIGHT " << height << std::endl;
	for(i=0; i<=nbtabs; i++) output << "\t";
	output << "ANGLE " << (PointCoordinateType)(CC_RAD_TO_DEG)*angle << std::endl;
	for(i=0; i<=nbtabs; i++) output << "\t";
	output << "AT X " << position[0] << " Y " << position[1] << " Z " << position[2] << std::endl;
	for(i=0; i<=nbtabs; i++) output << "\t";
	output << "ORI ";
	output << "X is X " << orientation[0][0] << " Y " << orientation[0][1] << " Z " << orientation[0][2];
	output << " AND Z is X " << orientation[2][0] << " Y " << orientation[2][1] << " Z " << orientation[2][2] << std::endl;

	for(i=0; i<nbtabs; i++) output << "\t";
	output << "END" << std::endl;
	return std::pair<int,int>(0,1);
}



Dish::Dish()
{
    diameter = 0.;
    height = 0.;
    radius = 0.;
}

bool Dish::setValue(Token t, PointCoordinateType value)
{
    switch(t)
    {
        case PDMS_HEIGHT: height=value; break;
        case PDMS_RADIUS: radius=value; break;
        case PDMS_DIAMETER: diameter=value; break;
        default: return false;
    }
    return true;
}

PointCoordinateType Dish::surface() const
{
	PointCoordinateType a, r;
	if(radius>ZERO_TOLERANCE)
	{
		r = (PointCoordinateType)0.5*diameter;
		if(fabs((2*height)-diameter)<ZERO_TOLERANCE)
			return (PointCoordinateType)(2.0*M_PI)*sqr(r);
		if((2*height)>diameter)
		{
			a = acos(r/height);
			return (PointCoordinateType)M_PI*(sqr(r)+(a*r*height/sin(a)));
		}
		else
		{
			a = acos(height/r);
			return (PointCoordinateType)M_PI*(sqr(r)+((sqr(height)/sin(a))*log((1+sin(a))/cos(a))));
		}
	}
	return (PointCoordinateType)M_PI*diameter*height;
}

std::pair<int,int> Dish::write(std::ostream &output, int nbtabs) const
{
	int i;

	for(i=0; i<nbtabs; i++) output << "\t";
	output << "NEW DISH";
	if(strlen(name))
		output << " /" << name;
	output << std::endl;

	for(i=0; i<=nbtabs; i++) output << "\t";
	output << "HEIGHT " << height << std::endl;
	for(i=0; i<=nbtabs; i++) output << "\t";
	output << "RADIUS " << radius << std::endl;
	for(i=0; i<=nbtabs; i++) output << "\t";
	output << "DIAMETER " << diameter << std::endl;
	for(i=0; i<=nbtabs; i++) output << "\t";
	output << "AT X " << position[0] << " Y " << position[1] << " Z " << position[2] << std::endl;
	for(i=0; i<=nbtabs; i++) output << "\t";
	output << "ORI ";
	output << "X is X " << orientation[0][0] << " Y " << orientation[0][1] << " Z " << orientation[0][2];
	output << " AND Z is X " << orientation[2][0] << " Y " << orientation[2][1] << " Z " << orientation[2][2] << std::endl;

	for(i=0; i<nbtabs; i++) output << "\t";
	output << "END" << std::endl;
	return std::pair<int,int>(0,1);
}



bool Cone::setValue(Token t, PointCoordinateType value)
{
    switch(t)
    {
        case PDMS_TOP_DIAMETER: dtop=value; break;
        case PDMS_BOTTOM_DIAMETER: dbottom=value; break;
        case PDMS_HEIGHT: height=value; break;
        default: return false;
    }
    return true;
}

PointCoordinateType Cone::surface() const
{
	PointCoordinateType a1, a2, h1, r1, r2;
	if(dtop<dbottom) { r1=dtop; r2=dbottom; }
	else { r1=dbottom; r2=dtop; }
	h1 = (r1*height)/(r2-r1);
	a1 = (PointCoordinateType)M_PI*r1*sqrt(sqr(r1)+sqr(h1));
	a2 = (PointCoordinateType)M_PI*r2*sqrt(sqr(r2)+sqr(h1+height));
	return a2-a1;
}

std::pair<int,int> Cone::write(std::ostream &output, int nbtabs) const
{
	int i;

	for(i=0; i<nbtabs; i++) output << "\t";
	output << "NEW CONE";
	if(strlen(name))
		output << " /" << name;
	output << std::endl;

	for(i=0; i<=nbtabs; i++) output << "\t";
	output << "HEIGHT " << height << std::endl;
	for(i=0; i<=nbtabs; i++) output << "\t";
	output << "DBOTTOM " << dbottom << std::endl;
	for(i=0; i<=nbtabs; i++) output << "\t";
	output << "DTOP " << dtop << std::endl;
	for(i=0; i<=nbtabs; i++) output << "\t";
	output << "AT X " << position[0] << " Y " << position[1] << " Z " << position[2] << std::endl;
	for(i=0; i<=nbtabs; i++) output << "\t";
	output << "ORI ";
	output << "X is X " << orientation[0][0] << " Y " << orientation[0][1] << " Z " << orientation[0][2];
	output << " AND Z is X " << orientation[2][0] << " Y " << orientation[2][1] << " Z " << orientation[2][2] << std::endl;

	for(i=0; i<nbtabs; i++) output << "\t";
	output << "END" << std::endl;
	return std::pair<int,int>(0,1);
}



Box::Box()
	: lengths((PointCoordinateType)0)
{
}

//Box::Box(const BPlane &p, PointCoordinateType height)
//{
//	position = p.getPosition();
//	orientation[0] = p.getAxis(Shape::X_AXIS);
//	orientation[1] = p.getAxis(Shape::Y_AXIS);
//	orientation[2] = p.getAxis(Shape::Z_AXIS);
//	lengths[0] = p.getWidth();
//	lengths[1] = p.getLength();
//	lengths[2] = height;
//	if(p.getName())
//		strcpy(name, p.getName());
//}

//BPlane* Box::getPlane(unsigned i) const
//{
//	unsigned ni=(i/2), di=(i%2);
//
//	BPlane *result = NULL;
//	try{ result = new BPlane(); }
//	catch(std::exception &nex) {memfail(nex,1);}
//	result->setAxis(orientation[(ni+1)%3], Shape::X_AXIS, orientation[(ni+2)%3], Shape::Y_AXIS);
//	if(di)
//		result->setPosition(position-(PointCoordinateType)0.5*lengths[ni]*orientation[ni]);
//	else
//		result->setPosition(position+(PointCoordinateType)0.5*lengths[ni]*orientation[ni]);
//	result->setWidth(lengths[(ni+1)%3]);
//	result->setLength(lengths[(ni+2)%3]);
//	result->setName(name);
//	return result;
//}

//Model* Box::toModel(unsigned filter)
//{
//	unsigned i, k;
//	BPlane *plane = 0;
//	Model *model = NULL;
//
//	if(!Shape::testMask(filter,Shape::PLANE))
//		return NULL;
//	for(i=0,k=4; i<3; i++)
//		if(abs(lengths[i])<=ZERO_TOLERANCE)
//			k = i;
//	model = new Model;
//	if(k>3)
//	{
//		for(i=0; i<6; i++)
//		{
//			plane = getPlane(i);
//			if(!model->pushElement(plane))
//			{
//				delete plane;
//				delete model;
//				return NULL;
//			}
//		}
//	}
//	else
//	{
//		plane = getPlane(2*k);
//		if(!model->pushElement(plane))
//		{
//			delete plane;
//			delete model;
//			return NULL;
//		}
//	}
//	return model;
//}

bool Box::setValue(Token t, PointCoordinateType value)
{
    switch(t)
    {
        case PDMS_XLENGTH: lengths[0]=value; break;
        case PDMS_YLENGTH: lengths[1]=value; break;
        case PDMS_ZLENGTH: lengths[2]=value; break;
        default: return false;
    }
    return true;
}

PointCoordinateType Box::surface() const
{
	return 2*((lengths[0]*lengths[1])+(lengths[1]*lengths[2])+(lengths[2]*lengths[0]));
}

std::pair<int,int> Box::write(std::ostream &output, int nbtabs) const
{
	int i;

	for(i=0; i<nbtabs; i++) output << "\t";
	if(negative)
		output << "NEW NBOX";
	else
		output << "NEW BOX";
	if(strlen(name))
		output << " /" << name;
	output << std::endl;

	for(i=0; i<=nbtabs; i++) output << "\t";
	output << "XLENGTH " << lengths[0] << std::endl;
	for(i=0; i<=nbtabs; i++) output << "\t";
	output << "YLENGTH " << lengths[1] << std::endl;
	for(i=0; i<=nbtabs; i++) output << "\t";
	output << "ZLENGTH " << lengths[2] << std::endl;
	for(i=0; i<=nbtabs; i++) output << "\t";
	output << "AT X " << position[0] << " Y " << position[1] << " Z " << position[2] << std::endl;
	for(i=0; i<=nbtabs; i++) output << "\t";
	output << "ORI ";
	output << "X is X " << orientation[0][0] << " Y " << orientation[0][1] << " Z " << orientation[0][2];
	output << " AND Z is X " << orientation[2][0] << " Y " << orientation[2][1] << " Z " << orientation[2][2] << std::endl;

	for(i=0; i<nbtabs; i++) output << "\t";
	output << "END" << std::endl;
	return std::pair<int,int>(0,1);
}


std::pair<int,int> Vertex::write(std::ostream &output, int nbtabs) const
{
	return std::pair<int,int>(0,0);
}


bool Loop::push(GenericItem *i)
{
    if(i->getType()==PDMS_VERTEX)
    {
        loop.push_back(dynamic_cast<Vertex*>(i));
        if(i->owner) i->owner->remove(i);
        i->owner=this;
		return true;
	}
	return false;
}

void Loop::remove(GenericItem *i)
{
    std::list<Vertex*>::iterator it;
	for(it=loop.begin(); it!=loop.end();)
    {
		if((*it)==i)
			loop.erase(it);
		else
			it++;
    }
}


std::pair<int,int> Loop::write(std::ostream &output, int nbtabs) const
{
	return std::pair<int,int>(0,0);
}

bool Extrusion::push(GenericItem *l)
{
    if(l->getType()==PDMS_LOOP)
    {
        if(loop) return false;
        loop=dynamic_cast<Loop*>(l);
        if(l->owner) l->owner->remove(l);
        l->owner=this;
        return true;
    }
	return DesignElement::push(l);
}

PointCoordinateType Extrusion::surface() const
{
	PointCoordinateType p;
	std::list<Vertex*>::const_iterator it1, it2;

	p=0.;
	if(loop)
	{
		it1 = it2 = loop->loop.begin();
		it2++;
		while(it1 != loop->loop.end())
		{
			if(it2 == loop->loop.end())
				it2 = loop->loop.begin();
			p += ((*it1)->v-(*it2)->v).norm();
			it1++;
			it2++;
		}
	}
	return p*height;
}

std::pair<int,int> Extrusion::write(std::ostream &output, int nbtabs) const
{
	return std::pair<int,int>(0,0);
}



bool Pyramid::setValue(Token t, PointCoordinateType value)
{
    switch(t)
    {
        case PDMS_X_BOTTOM:xbot=value; break;
        case PDMS_Y_BOTTOM:ybot=value; break;
        case PDMS_X_TOP:xtop=value; break;
        case PDMS_Y_TOP:ytop=value; break;
        case PDMS_X_OFF:xoff=value; break;
        case PDMS_Y_OFF:yoff=value; break;
        case PDMS_HEIGHT:height=value; break;
        default:
            return false;
    }
    return true;
}

PointCoordinateType Pyramid::surface() const
{
	//TODO
	return 0;
}

std::pair<int,int> Pyramid::write(std::ostream &output, int nbtabs) const
{
	return std::pair<int,int>(0,0);
}


bool Snout::setValue(Token t, PointCoordinateType value)
{
    switch(t)
    {
        case PDMS_BOTTOM_DIAMETER:dbottom=value; break;
        case PDMS_TOP_DIAMETER:dtop=value; break;
        case PDMS_X_OFF:xoff=value; break;
        case PDMS_Y_OFF:yoff=value; break;
        case PDMS_HEIGHT:height=value; break;
        default:
            return false;
    }
    return true;
}

PointCoordinateType Snout::surface() const
{
	//TODO
	return 0;
}

std::pair<int,int> Snout::write(std::ostream &output, int nbtabs) const
{
	return std::pair<int,int>(0,0);
}
