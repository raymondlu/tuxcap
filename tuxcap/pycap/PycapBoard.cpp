//--------------------------------------------------
// PycapBoard
//
// Base game board for Pycap
// Manages hooks to python game code
//
// Original bindings by Jarrad "Farbs" Woods
//--------------------------------------------------

// includes
#include "PycapBoard.h"
#include "PycapApp.h"

#include "Graphics.h"
#include <Python.h>


#include <math.h>


// namespace
using namespace Sexy;


// functions

//--------------------------------------------------
// PycapBoard
//--------------------------------------------------
PycapBoard::PycapBoard()
{
	// call python game init function
	PyObject* pInitFunc = PyDict_GetItemString( PycapApp::sApp->pDict, "init" );

    if ( pInitFunc )
	{
		if ( PyCallable_Check( pInitFunc ) )
		{
			PyObject_CallObject( pInitFunc, NULL );
		}
		else
		{
			//PycapApp::sApp->Popup( StrFormat( "\"init\" found, but not callable" ) );
		}
	}

	// grab frequently used python functions
	pUpdateFunc = PyDict_GetItemString( PycapApp::sApp->pDict, "update" );
	pDrawFunc = PyDict_GetItemString( PycapApp::sApp->pDict, "draw" );
	pKeyDownFunc = PyDict_GetItemString( PycapApp::sApp->pDict, "keyDown" );
	pKeyUpFunc = PyDict_GetItemString( PycapApp::sApp->pDict, "keyUp" );
	pExitGame = PyDict_GetItemString( PycapApp::sApp->pDict, "exitGame" );
	pMouseEnterFunc = PyDict_GetItemString( PycapApp::sApp->pDict, "mouseEnter" );
	pMouseLeaveFunc = PyDict_GetItemString( PycapApp::sApp->pDict, "mouseLeave" );
	pMouseMoveFunc = PyDict_GetItemString( PycapApp::sApp->pDict, "mouseMove" );
	pMouseDownFunc = PyDict_GetItemString( PycapApp::sApp->pDict, "mouseDown" );
	pMouseUpFunc = PyDict_GetItemString( PycapApp::sApp->pDict, "mouseUp" );
	pMouseWheelFunc = PyDict_GetItemString( PycapApp::sApp->pDict, "mouseWheel" );
	// legacy spelling
	if( !pKeyDownFunc )
		pKeyDownFunc = PyDict_GetItemString( PycapApp::sApp->pDict, "keydown" );
	if( !pKeyUpFunc )
		pKeyUpFunc = PyDict_GetItemString( PycapApp::sApp->pDict, "keyup" );


	// init remaining members
	graphics	= NULL;

	// general error location warning
	if (PyErr_Occurred())
	{
		PyErr_Print();
		//PycapApp::sApp->Popup( StrFormat( "Some kind of python error occurred in PycapBoard()." ) );
	}

	// request initial draw
	MarkDirty();
}

//--------------------------------------------------
// ~PycapBoard
//--------------------------------------------------
PycapBoard::~PycapBoard()
{
	// call python shutdown function
	PyObject* pFiniFunc = PyDict_GetItemString( PycapApp::sApp->pDict, "fini" );

    if ( pFiniFunc && PyCallable_Check( pFiniFunc ) )
	{
        PyObject_CallObject( pFiniFunc, NULL );
    }
	// general error location warning
	if (PyErr_Occurred())
	{
		PyErr_Print();
		//PycapApp::sApp->Popup( StrFormat( "Some kind of python error occurred in ~PycapBoard()." ) );
	}
}

//--------------------------------------------------
// Update
//--------------------------------------------------
void PycapBoard::UpdateF( float delta )
{
	// call parent
	Widget::UpdateF( delta );

	// Python exit-check
	// Checked on entering incase a non-update function has set it
	if ( pExitGame )
	{
		PyObject* pExit = PyObject_CallObject( pExitGame, NULL );
		if ( PyInt_Check( pExit ) && PyInt_AsLong( pExit ) != 0 )
		{
			// drop the return value
			Py_DECREF( pExit );
			
			// exit the program
			PycapApp::sApp->Shutdown();

			// no need to update
			return;
		}
		// drop the return value
		Py_DECREF( pExit );
	}	

	// Python update hook
	// The python code should call dirty if the screen needs to be redrawn
	if ( pUpdateFunc )
	{
		PyObject* pArgs = PyTuple_New(1);
		PyObject* pDelta = PyFloat_FromDouble( delta );
		PyTuple_SetItem( pArgs, 0, pDelta );
		PyObject_CallObject( pUpdateFunc, pArgs );
		Py_DECREF( pDelta );
		Py_DECREF( pArgs );
	}
	
	// Python exit-check
	// Checked on exiting updatef incase it has set it
	if ( pExitGame )
	{
		PyObject* pExit = PyObject_CallObject( pExitGame, NULL );
		if ( PyInt_Check( pExit ) && PyInt_AsLong( pExit ) != 0 )
		{
			// drop the return value
			Py_DECREF( pExit );
			
			// exit the program
			PycapApp::sApp->Shutdown();

			// no need to update
			return;
		}
		// drop the return value
		Py_DECREF( pExit );
	}

	// general error location warning
	if (PyErr_Occurred())
	{
		PyErr_Print();
		//PycapApp::sApp->Popup( StrFormat( "Some kind of python error occurred in Update." ) );
		PycapApp::sApp->Shutdown();
	}
}

//--------------------------------------------------
// Draw
//--------------------------------------------------
void PycapBoard::Draw( Graphics *g )
{
	// exit early if no python draw function
	if ( !pDrawFunc )
		return;

	// enter draw code
	graphics = g;

	// call draw function
    PyObject_CallObject( pDrawFunc, NULL );

	// exit draw code
	graphics = NULL;

	// general error location warning
	if (PyErr_Occurred())
	{
		PyErr_Print();
		//PycapApp::sApp->Popup( StrFormat( "Some kind of python error occurred in Draw." ) );
		PycapApp::sApp->Shutdown();
	}
}

//--------------------------------------------------
// KeyDown
//--------------------------------------------------
void PycapBoard::KeyDown( KeyCode key )
{
	// exit early if no python keydown function
	if ( !pKeyDownFunc )
		return;

	// Python keydown hook
	PyObject* pArgs = PyTuple_New(1);
	PyObject* pKey = PyInt_FromLong( key );
	PyTuple_SetItem( pArgs, 0, pKey );
    PyObject_CallObject( pKeyDownFunc, pArgs );
	Py_DECREF( pArgs );

	// general error location warning
	if (PyErr_Occurred())
	{
		PyErr_Print();
		//PycapApp::sApp->Popup( StrFormat( "Some kind of python error occurred in KeyDown." ) );
		PycapApp::sApp->Shutdown();
	}
}

//--------------------------------------------------
// KeyUp
//--------------------------------------------------
void PycapBoard::KeyUp( KeyCode key )
{
	// exit early if no python keyup function
	if ( !pKeyUpFunc )
		return;

	// Python keyup hook
	PyObject* pArgs = PyTuple_New(1);
	PyObject* pKey = PyInt_FromLong( key );
	PyTuple_SetItem( pArgs, 0, pKey );
    PyObject_CallObject( pKeyUpFunc, pArgs );
	Py_DECREF( pArgs );

	// general error location warning
	if (PyErr_Occurred())
	{
		PyErr_Print();
		//PycapApp::sApp->Popup( StrFormat( "Some kind of python error occurred in KeyUp." ) );
		PycapApp::sApp->Shutdown();
	}
}

//--------------------------------------------------
// MouseEnter
//--------------------------------------------------
void PycapBoard::MouseEnter()
{
	// call python function if it exists
	if( pMouseEnterFunc )
		PyObject_CallObject( pMouseEnterFunc, NULL );

	// general error location warning
	if (PyErr_Occurred())
	{
		PyErr_Print();
		//PycapApp::sApp->Popup( StrFormat( "Some kind of python error occurred in MouseEnter." ) );
		PycapApp::sApp->Shutdown();
	}
}

//--------------------------------------------------
// MouseLeave
//--------------------------------------------------
void PycapBoard::MouseLeave()
{
	// call python function if it exists
	if( pMouseLeaveFunc )
		PyObject_CallObject( pMouseLeaveFunc, NULL );

	// general error location warning
	if (PyErr_Occurred())
	{
		PyErr_Print();
		//PycapApp::sApp->Popup( StrFormat( "Some kind of python error occurred in MouseLeave." ) );
		PycapApp::sApp->Shutdown();
	}
}

//--------------------------------------------------
// MouseMove
//--------------------------------------------------
void PycapBoard::MouseMove( int x, int y )
{
	// Python mouse move hook
	if( pMouseMoveFunc )
	{
		PyObject* pArgs = PyTuple_New(2);
		PyObject* pX = PyInt_FromLong( x );
		PyObject* pY = PyInt_FromLong( y );
		PyTuple_SetItem( pArgs, 0, pX );
		PyTuple_SetItem( pArgs, 1, pY );
		PyObject_CallObject( pMouseMoveFunc, pArgs );
		Py_DECREF( pArgs );

		// general error location warning
		if (PyErr_Occurred())
		{
			PyErr_Print();
			//PycapApp::sApp->Popup( StrFormat( "Some kind of python error occurred in MouseMove." ) );
			PycapApp::sApp->Shutdown();
		}
	}
}

//--------------------------------------------------
// MouseDrag
//--------------------------------------------------
void PycapBoard::MouseDrag( int x, int y )
{
	// This gets called instead of mousemove when dragging.
	// For our purposes, they're the same... so do the same thing!
	MouseMove( x, y );
}

//--------------------------------------------------
// MouseDown
//--------------------------------------------------
void PycapBoard::MouseDown(int x, int y, int theBtnNum, int theClickCount)
{
	// Python mouse down hook
	if( pMouseDownFunc )
	{
		PyObject* pArgs = PyTuple_New(3);
		PyObject* pX = PyInt_FromLong( x );
		PyObject* pY = PyInt_FromLong( y );
		PyObject* pButton = PyInt_FromLong( theBtnNum );
		PyTuple_SetItem( pArgs, 0, pX );
		PyTuple_SetItem( pArgs, 1, pY );
		PyTuple_SetItem( pArgs, 2, pButton );
		PyObject_CallObject( pMouseDownFunc, pArgs );
		Py_DECREF( pArgs );

		// general error location warning
		if (PyErr_Occurred())
		{
			PyErr_Print();
			//PycapApp::sApp->Popup( StrFormat( "Some kind of python error occurred in MouseDown." ) );
			PycapApp::sApp->Shutdown();
		}
	}
}

//--------------------------------------------------
// MouseUp
//--------------------------------------------------
void PycapBoard::MouseUp(int x, int y, int theBtnNum, int theClickCount)
{
	// Python mouse up hook
	if( pMouseUpFunc )
	{
		PyObject* pArgs = PyTuple_New(3);
		PyObject* pX = PyInt_FromLong( x );
		PyObject* pY = PyInt_FromLong( y );
		PyObject* pButton = PyInt_FromLong( theBtnNum );
		PyTuple_SetItem( pArgs, 0, pX );
		PyTuple_SetItem( pArgs, 1, pY );
		PyTuple_SetItem( pArgs, 2, pButton );
		PyObject_CallObject( pMouseUpFunc, pArgs );
		Py_DECREF( pArgs );

		// general error location warning
		if (PyErr_Occurred())
		{
			PyErr_Print();
			//PycapApp::sApp->Popup( StrFormat( "Some kind of python error occurred in MouseUp." ) );
			PycapApp::sApp->Shutdown();
		}
	}
}

//--------------------------------------------------
// MouseWheel
//--------------------------------------------------
void PycapBoard::MouseWheel( int delta)
{
	// Python mouse move hook
	if( pMouseWheelFunc )
	{
		PyObject* pArgs = PyTuple_New(1);
		PyObject* pX = PyInt_FromLong( delta );
		PyTuple_SetItem( pArgs, 0, pX );
		PyObject_CallObject( pMouseWheelFunc, pArgs );
		Py_DECREF( pArgs );

		// general error location warning
		if (PyErr_Occurred())
		{
			PyErr_Print();
			//PycapApp::sApp->Popup( StrFormat( "Some kind of python error occurred in MouseWheel." ) );
			PycapApp::sApp->Shutdown();
		}
	}
}