/*=========================================================================

  Program:   Visualization Toolkit
  Module:    vtkPVFileEntry.cxx
  Language:  C++
  Date:      $Date$
  Version:   $Revision$

Copyright (c) 2000-2001 Kitware Inc. 469 Clifton Corporate Parkway,
Clifton Park, NY, 12065, USA.
All rights reserved.

Redistribution and use in source and binary forms, with or without
modification, are permitted provided that the following conditions are met:

 * Redistributions of source code must retain the above copyright notice,
   this list of conditions and the following disclaimer.

 * Redistributions in binary form must reproduce the above copyright notice,
   this list of conditions and the following disclaimer in the documentation
   and/or other materials provided with the distribution.

 * Neither the name of Kitware nor the names of any contributors may be used
   to endorse or promote products derived from this software without specific 
   prior written permission.

 * Modified source versions must be plainly marked as such, and must not be
   misrepresented as being the original software.

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS ``AS IS''
AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
ARE DISCLAIMED. IN NO EVENT SHALL THE AUTHORS OR CONTRIBUTORS BE LIABLE FOR
ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

=========================================================================*/
#include "vtkPVFileEntry.h"

#include "vtkArrayMap.txx"
#include "vtkKWDirectoryUtilities.h"
#include "vtkKWEntry.h"
#include "vtkKWFrame.h"
#include "vtkKWLabel.h"
#include "vtkKWLoadSaveDialog.h"
#include "vtkKWMenu.h"
#include "vtkKWPushButton.h"
#include "vtkKWScale.h"
#include "vtkObjectFactory.h"
#include "vtkPVAnimationInterfaceEntry.h"
#include "vtkPVApplication.h"
#include "vtkPVFileEntryProperty.h"
#include "vtkPVProcessModule.h"
#include "vtkPVReaderModule.h"
#include "vtkPVWindow.h"
#include "vtkPVXMLElement.h"
#include "vtkString.h"
#include "vtkStringList.h"
#include "vtkKWListSelectOrder.h"
#include "vtkCommand.h"
#include "vtkKWPopupButton.h"
#include "vtkKWLabeledFrame.h"

//===========================================================================
//***************************************************************************
class vtkPVFileEntryObserver: public vtkCommand
{
public:
  static vtkPVFileEntryObserver *New() 
    {return new vtkPVFileEntryObserver;};

  vtkPVFileEntryObserver()
    {
    this->FileEntry= 0;
    }

  virtual void Execute(vtkObject* wdg, unsigned long event,  
    void* calldata)
    {
    if ( this->FileEntry)
      {
      this->FileEntry->ExecuteEvent(wdg, event, calldata);
      }
    }

  vtkPVFileEntry* FileEntry;
};

//***************************************************************************
//===========================================================================

//----------------------------------------------------------------------------
vtkStandardNewMacro(vtkPVFileEntry);
vtkCxxRevisionMacro(vtkPVFileEntry, "1.66");

//----------------------------------------------------------------------------
vtkPVFileEntry::vtkPVFileEntry()
{
  this->Observer = vtkPVFileEntryObserver::New();
  this->Observer->FileEntry = this;
  this->LabelWidget = vtkKWLabel::New();
  this->Entry = vtkKWEntry::New();
  //this->Entry->PullDownOn();
  this->BrowseButton = vtkKWPushButton::New();
  this->Extension = NULL;
  this->InSetValue = 0;

  this->TimestepFrame = vtkKWFrame::New();
  this->Timestep = vtkKWScale::New();
  this->TimeStep = 0;
  
  this->Property = NULL;

  this->Path = 0;

  this->FileListPopup = vtkKWPopupButton::New();

  this->FileListSelect = vtkKWListSelectOrder::New();
  this->ListObserverTag = 0;
  this->IgnoreFileListEvents = 0;
}

//----------------------------------------------------------------------------
vtkPVFileEntry::~vtkPVFileEntry()
{
  if ( this->ListObserverTag )
    {
    this->FileListSelect->RemoveObserver(this->ListObserverTag);
    }
  this->Observer->FileEntry = 0;
  this->Observer->Delete();
  this->Observer = 0;
  this->BrowseButton->Delete();
  this->BrowseButton = NULL;
  this->Entry->Delete();
  this->Entry = NULL;
  this->LabelWidget->Delete();
  this->LabelWidget = NULL;
  this->SetExtension(NULL);

  this->Timestep->Delete();
  this->TimestepFrame->Delete();
  this->FileListPopup->Delete();
  this->FileListPopup = 0;
  this->FileListSelect->Delete();
  this->FileListSelect = 0;
  
  this->SetProperty(NULL);

  this->SetPath(0);
}

//----------------------------------------------------------------------------
void vtkPVFileEntry::SetLabel(const char* label)
{
  // For getting the widget in a script.
  this->LabelWidget->SetLabel(label);

  if (label && label[0] &&
      (this->TraceNameState == vtkPVWidget::Uninitialized ||
       this->TraceNameState == vtkPVWidget::Default) )
    {
    this->SetTraceName(label);
    this->SetTraceNameState(vtkPVWidget::SelfInitialized);
    }
}

//----------------------------------------------------------------------------
const char* vtkPVFileEntry::GetLabel()
{
  return this->LabelWidget->GetLabel();
}

void vtkPVFileEntry::SetBalloonHelpString(const char *str)
{

  // A little overkill.
  if (this->BalloonHelpString == NULL && str == NULL)
    {
    return;
    }

  // This check is needed to prevent errors when using
  // this->SetBalloonHelpString(this->BalloonHelpString)
  if (str != this->BalloonHelpString)
    {
    // Normal string stuff.
    if (this->BalloonHelpString)
      {
      delete [] this->BalloonHelpString;
      this->BalloonHelpString = NULL;
      }
    if (str != NULL)
      {
      this->BalloonHelpString = new char[strlen(str)+1];
      strcpy(this->BalloonHelpString, str);
      }
    }
  
  if ( this->Application && !this->BalloonHelpInitialized )
    {
    this->LabelWidget->SetBalloonHelpString(this->BalloonHelpString);
    this->Entry->SetBalloonHelpString(this->BalloonHelpString);
    this->BrowseButton->SetBalloonHelpString(this->BalloonHelpString);
    this->BalloonHelpInitialized = 1;
    }
}

//----------------------------------------------------------------------------
void vtkPVFileEntry::Create(vtkKWApplication *pvApp)
{
  const char* wname;
  
  if (this->Application)
    {
    vtkErrorMacro("FileEntry already created");
    return;
    }
  
  this->SetApplication(pvApp);

  // create the top level
  wname = this->GetWidgetName();
  this->Script("frame %s -borderwidth 0 -relief flat", wname);
  vtkKWFrame* frame = vtkKWFrame::New();
  frame->SetParent(this);
  frame->Create(pvApp, 0);

  this->LabelWidget->SetParent(frame);
  this->Entry->SetParent(frame);
  this->BrowseButton->SetParent(frame);
  
  // Now a label
  this->LabelWidget->Create(pvApp, "-width 18 -justify right");
  this->Script("pack %s -side left", this->LabelWidget->GetWidgetName());
  
  // Now the entry
  this->Entry->Create(pvApp, "");
  this->Script("bind %s <KeyPress> {%s ModifiedCallback}",
               this->Entry->GetWidgetName(), this->GetTclName());
  this->Entry->BindCommand(this, "EntryChangedCallback");
  // Change the order of the bindings so that the
  // modified command gets called after the entry changes.
  this->Script("bindtags %s [concat Entry [lreplace [bindtags %s] 1 1]]", 
               this->Entry->GetWidgetName(), this->Entry->GetWidgetName());
  this->Script("pack %s -side left -fill x -expand t",
               this->Entry->GetWidgetName());
  
  // Now the push button
  this->BrowseButton->Create(pvApp, "");
  this->BrowseButton->SetLabel("Browse");
  this->BrowseButton->SetCommand(this, "BrowseCallback");

  if (this->BalloonHelpString)
    {
    this->SetBalloonHelpString(this->BalloonHelpString);
    }
  this->Script("pack %s -side left", this->BrowseButton->GetWidgetName());
  this->Script("pack %s -fill both -expand 1", frame->GetWidgetName());

  this->TimestepFrame->SetParent(this);
  this->TimestepFrame->Create(pvApp, 0);
  this->Timestep->SetParent(this->TimestepFrame);
  this->Timestep->Create(pvApp, 0);
  this->Script("pack %s -expand 1 -fill both", this->Timestep->GetWidgetName());
  this->Script("pack %s -side bottom -expand 1 -fill x", this->TimestepFrame->GetWidgetName());
  this->Script("pack forget %s", this->TimestepFrame->GetWidgetName());
  this->Timestep->DisplayLabel("Timestep");
  this->Timestep->DisplayRangeOn();
  this->Timestep->DisplayEntryAndLabelOnTopOff();
  this->Timestep->DisplayEntry();
  this->Timestep->SetEndCommand(this, "TimestepChangedCallback");
  this->Timestep->SetEntryCommand(this, "TimestepChangedCallback");

  this->FileListPopup->SetParent(frame);
  this->FileListPopup->Create(pvApp, 0);
  this->FileListPopup->SetLabel("Timesteps");
  this->FileListPopup->SetPopupTitle("Select Files For Time Series");
  this->FileListPopup->SetCommand(this, "UpdateAvailableFiles");

  this->FileListSelect->SetParent(this->FileListPopup->GetPopupFrame());
  this->FileListSelect->Create(pvApp, 0);
  this->Script("pack %s -fill both -expand 1", this->FileListSelect->GetWidgetName());
  this->Script("pack %s -fill x", this->FileListPopup->GetWidgetName());

  this->ListObserverTag = this->FileListSelect->AddObserver(vtkCommand::ModifiedEvent, 
    this->Observer);
  frame->Delete();
}


//----------------------------------------------------------------------------
void vtkPVFileEntry::EntryChangedCallback()
{
  const char* val = this->Entry->GetValue();
  this->SetValue(val);
}

//-----------------------------------------------------------------------------
void vtkPVFileEntry::SetTimeStep(int ts)
{
  if ( ts >= this->Property->GetNumberOfFiles() && ts < 0 )
    {
    return;
    }
  this->TimeStep = ts;
  const char* fname = this->Property->GetFile(this->TimeStep);
  if ( fname )
    {
    if ( fname[0] == '/' || 
      (fname[1] == ':' && (fname[2] == '/' || fname[2] == '\\')) ||
      (fname[0] == '\\' && fname[1] == '\\') )
      {
      this->SetValue(fname);
      }
    else
      {
      ostrstream str;
      str << this->Path << "/" << fname << ends;
      this->SetValue(str.str());
      str.rdbuf()->freeze(0);
      }
    }
}

//----------------------------------------------------------------------------
void vtkPVFileEntry::TimestepChangedCallback()
{
  int ts = static_cast<int>(this->Timestep->GetValue());
  this->SetTimeStep(ts);
}

//----------------------------------------------------------------------------
void vtkPVFileEntry::BrowseCallback()
{
  ostrstream str;
  vtkPVProcessModule* pm = this->GetPVApplication()->GetProcessModule();
  vtkKWLoadSaveDialog* loadDialog = pm->NewLoadSaveDialog();
  const char* fname = this->Entry->GetValue();

  vtkPVApplication* pvApp = pm->GetPVApplication();
  vtkPVWindow* win = 0;
  if (pvApp)
    {
    win = pvApp->GetMainWindow();
    }
  if (fname && fname[0])
    {
    char* path   = new char [ strlen(fname) + 1];
    vtkKWDirectoryUtilities::GetFilenamePath(fname, path);
    if (path[0])
      {
      loadDialog->SetLastPath( path );
      }
    delete[] path;
    }
  else
    {
    if (win)
      {
      win->RetrieveLastPath(loadDialog, "OpenPath");
      }
    }
  loadDialog->Create(this->GetPVApplication(), 0);
  if (win) 
    { 
    loadDialog->SetParent(this); 
    }
  loadDialog->SetTitle(this->GetLabel()?this->GetLabel():"Select File");
  if(this->Extension)
    {
    loadDialog->SetDefaultExtension(this->Extension);
    str << "{{} {." << this->Extension << "}} ";
    }
  str << "{{All files} {*}}" << ends;  
  loadDialog->SetFileTypes(str.str());
  str.rdbuf()->freeze(0);  
  if(loadDialog->Invoke())
    {
    this->Script("%s SetValue {%s}", this->GetTclName(),
                 loadDialog->GetFileName());
    }
  loadDialog->Delete();
}

//----------------------------------------------------------------------------
void vtkPVFileEntry::SetValue(const char* fileName)
{
  if ( this->InSetValue )
    {
    return;
    }

  const char *old;
  
  if (fileName == NULL || fileName[0] == 0)
    {
    return;
    }

  old = this->Entry->GetValue();
  if (strcmp(old, fileName) == 0)
    {
    return;
    }
  this->InSetValue = 1;

  this->Entry->SetValue(fileName); 

  int already_set = 0;
  int cc;

  char* prefix = 0;
  char* format = 0;
  int range[2];
  already_set = this->Property->GetNumberOfFiles();

  char* path   = new char [ strlen(fileName) + 1];
  vtkKWDirectoryUtilities::GetFilenamePath(fileName, path);

  if ( !this->Path || strcmp(this->Path, path) != 0 )
    {
    already_set = 0;
    this->SetPath(path);
    }

  if ( already_set )
    {
    // Already set, so just return
    this->InSetValue = 0;
    this->ModifiedCallback();
    delete [] path;
    return;
    }

  range[0] = range[1] = 0;

  this->IgnoreFileListEvents = 1;

  this->FileListSelect->RemoveItemsFromFinalList();

  // Have to regenerate prefix, pattern...

  vtkPVProcessModule* pm = this->GetPVApplication()->GetProcessModule();
  vtkStringList* files = vtkStringList::New();
  char* file   = new char [ strlen(fileName) + 1];
  char* ext    = new char [ strlen(fileName) + 1];
  char* number = new char [ strlen(fileName) + 1];
  vtkKWDirectoryUtilities::GetFilenameName(fileName, file);
  vtkKWDirectoryUtilities::GetFilenameExtension(fileName, ext);

  int in_ext = 1;
  int in_num = 0;

  int fnameLength = 0;

  int h5Flag = 0;
  if (strcmp(ext, "h5") == 0)
    {
    h5Flag = 1;
    file[strlen(file)-1] = 'f';
    }

  int ncnt = 0;
  for ( cc = (int)(strlen(file))-1; cc >= 0; cc -- )
    {
    if ( file[cc] >= '0' && file[cc] <= '9' )
      {
      in_num = 1;
      number[ncnt] = file[cc];
      ncnt ++;
      }
    else if ( in_ext && file[cc] == '.' )
      {
      in_ext = 0;
      in_num = 1;
      ncnt = 0;
      }
    else if ( in_num )
      {
      break;
      }
    file[cc] = 0;
    }

  if ( path[0] )
    {
    prefix = file;
    number[ncnt] = 0;
    for ( cc = 0; cc < ncnt/2; cc ++ )
      {
      char tmp = number[cc];
      number[cc] = number[ncnt-cc-1];
      number[ncnt-cc-1] = tmp;
      }
    char firstformat[100];
    char secondformat[100];
    sprintf(firstformat, "%%s/%%s%%0%dd.%%s", ncnt);
    sprintf(secondformat, "%%s/%%s%%d.%%s");
    this->Entry->DeleteAllValues();
    pm->GetDirectoryListing(path, 0, files, 0);
    int cnt = 0;
    for ( cc = 0; cc < files->GetLength(); cc ++ )
      {
      this->FileListSelect->AddSourceElement(files->GetString(cc));
      if ( vtkString::StartsWith(files->GetString(cc), file ) &&
        vtkString::EndsWith(files->GetString(cc), ext) )
        {
        cnt ++;
        }
      }
    int med = atoi(number);
    fnameLength = (int)(strlen(fileName)) * 2;
    char* rfname = new char[ fnameLength ];
    int min = med+cnt;
    int max = med-cnt;
    int foundone = 0;
    for ( cc = med-cnt; cc < med+cnt; cc ++ )
      {
      sprintf(rfname, firstformat, path, file, cc, ext);
      if ( files->GetIndex(rfname+strlen(path)+1) >= 0 )
        {
        this->Entry->AddValue(rfname);
        if ( max < cc )
          {
          max = cc;
          }
        if ( min > cc )
          {
          min = cc;
          }
        foundone = 1;
        }
      else if ( foundone )
        {
        if ( min > max || med < min || med > max )
          {
          min = cc;
          }
        else
          {
          break;
          }
        }
      }
    foundone = 0;
    int smin = med+cnt;
    int smax = med-cnt;
    for ( cc = med-cnt; cc < med+cnt; cc ++ )
      {
      sprintf(rfname, secondformat, path, file, cc, ext);
      if ( files->GetIndex(rfname+strlen(path)+1) >= 0 )
        {
        this->Entry->AddValue(rfname);
        if ( smax < cc )
          {
          smax = cc;
          }
        if ( smin > cc )
          {
          smin = cc;
          }
        foundone = 1;
        }
      else if ( foundone )
        {
        if ( smin > smax || med < smin || med > smax )
          {
          smin = cc;
          }
        else
          {
          break;
          }
        }
      }
    delete [] rfname;
    // If second range is bigger than first range, use second format
    if ( (smax - smin) >= (max - min) )
      {
      format = secondformat;
      min = smin;
      max = smax;
      // cout << "Use second format" << endl;
      }
    else
      {
      format = firstformat;
      // cout << "Use first format" << endl;
      }
    char* name = new char [ fnameLength ];
    char* shname = new char [ fnameLength ];
    for ( cc = min; cc <= max; cc ++ )
      {
      sprintf(name, format, path, prefix, cc, ext);
      vtkKWDirectoryUtilities::GetFilenameName(name, shname);
      if ( files->GetIndex(shname) >= 0 )
        {
        this->FileListSelect->AddFinalElement(shname, 1);
        }
      }
    delete [] name;
    delete [] shname;
    }

  if ( !this->FileListSelect->GetNumberOfElementsOnFinalList() )
    {
    vtkKWDirectoryUtilities::GetFilenameName(fileName, file);
    this->FileListSelect->AddFinalElement(file, 1);
    }

  files->Delete();
  delete [] path;
  delete [] file;
  delete [] ext;
  delete [] number;

  this->UpdateTimeStep();

  this->IgnoreFileListEvents = 0;
  this->InSetValue = 0;
  this->ModifiedCallback();
}

//---------------------------------------------------------------------------
void vtkPVFileEntry::Trace(ofstream *file)
{
  if ( ! this->InitializeTrace(file))
    {
    return;
    }

  // I assume the quotes are for eveluating an output tcl variable.
  *file << "$kw(" << this->GetTclName() << ") SetValue \""
        << this->GetValue() << "\"" << endl;
}


//----------------------------------------------------------------------------
void vtkPVFileEntry::AcceptInternal(vtkClientServerID sourceID)
{
  const char* fname = this->Entry->GetValue();
  char *cmd = new char[strlen(this->VariableName)+4];
  sprintf(cmd, "Set%s", this->VariableName);
  
  this->Property->SetString(fname);
  this->Property->SetVTKSourceID(sourceID);
  this->Property->SetTimeStep(this->TimeStep);
  this->Property->SetVTKCommand(cmd);

  delete[] cmd;
  
  vtkPVReaderModule* rm = vtkPVReaderModule::SafeDownCast(this->PVSource);
  if (rm && fname && fname[0])
    {
    const char* desc = rm->RemovePath(fname);
    if (desc)
      {
      rm->SetLabelOnce(desc);
      }
    }
  this->Property->RemoveAllFiles();
  int cc;
  for ( cc = 0; cc < this->FileListSelect->GetNumberOfElementsOnFinalList(); cc ++ )
    {
    this->Property->AddFile(this->FileListSelect->GetElementFromFinalList(cc));
    }

  if ( this->Property->GetNumberOfFiles() > 0 )
    {
    this->WidgetRange[0] = 0;
    this->WidgetRange[1] = this->FileListSelect->GetNumberOfElementsOnFinalList();
    this->UseWidgetRange = 1;
    }
  
  this->Property->AcceptInternal();
  this->UpdateAvailableFiles();
  
  this->ModifiedFlag = 0;
}


//----------------------------------------------------------------------------
void vtkPVFileEntry::ResetInternal()
{
  this->SetValue(this->Property->GetString());
  this->SetTimeStep(this->Property->GetTimeStep());

  this->IgnoreFileListEvents = 1;
  this->FileListSelect->RemoveItemsFromFinalList();
  int cc;
  for ( cc = 0; cc < this->Property->GetNumberOfFiles(); cc ++ )
    {
    this->FileListSelect->AddFinalElement(this->Property->GetFile(cc), 1);
    }
  const char* fileName = this->Entry->GetValue();
  if ( fileName && fileName[0] )
    {
    char *file = new char[ strlen(fileName) + 1 ];
    vtkKWDirectoryUtilities::GetFilenameName(fileName, file);
    this->FileListSelect->AddFinalElement(file, 1);
    delete [] file;
    }

  this->IgnoreFileListEvents = 0;

  this->UpdateAvailableFiles();

  this->ModifiedFlag = 0;
}

//----------------------------------------------------------------------------
vtkPVFileEntry* vtkPVFileEntry::ClonePrototype(vtkPVSource* pvSource,
                                 vtkArrayMap<vtkPVWidget*, vtkPVWidget*>* map)
{
  vtkPVWidget* clone = this->ClonePrototypeInternal(pvSource, map);
  return vtkPVFileEntry::SafeDownCast(clone);
}

//----------------------------------------------------------------------------
void vtkPVFileEntry::CopyProperties(vtkPVWidget* clone, vtkPVSource* pvSource,
                              vtkArrayMap<vtkPVWidget*, vtkPVWidget*>* map)
{
  this->Superclass::CopyProperties(clone, pvSource, map);
  vtkPVFileEntry* pvfe = vtkPVFileEntry::SafeDownCast(clone);
  if (pvfe)
    {
    pvfe->LabelWidget->SetLabel(this->LabelWidget->GetLabel());
    pvfe->SetExtension(this->GetExtension());
    }
  else 
    {
    vtkErrorMacro("Internal error. Could not downcast clone to PVFileEntry.");
    }
}

//----------------------------------------------------------------------------
int vtkPVFileEntry::ReadXMLAttributes(vtkPVXMLElement* element,
                                      vtkPVXMLPackageParser* parser)
{
  if(!this->Superclass::ReadXMLAttributes(element, parser)) { return 0; }
  
  // Setup the Label.
  const char* label = element->GetAttribute("label");
  if(label)
    {
    this->SetLabel(label);
    }
  else
    {
    this->SetLabel(this->VariableName);
    }
  
  // Setup the Extension.
  const char* extension = element->GetAttribute("extension");
  if(!extension)
    {
    vtkErrorMacro("No extension attribute.");
    return 0;
    }
  this->SetExtension(extension);
  
  return 1;
}

//----------------------------------------------------------------------------
const char* vtkPVFileEntry::GetValue() 
{
  return this->Entry->GetValue();
}

//-----------------------------------------------------------------------------
int vtkPVFileEntry::GetNumberOfFiles()
{
  return this->Property->GetNumberOfFiles();
}

//-----------------------------------------------------------------------------
void vtkPVFileEntry::SaveInBatchScriptForPart(ofstream* file,
                                              vtkClientServerID sourceID)
{
  if ( this->Property->GetNumberOfFiles() > 1 )
    {
    *file << "\tset " << "pvTemp" << sourceID << "_files {";
    int cc;
    for ( cc = 0; cc < this->Property->GetNumberOfFiles(); cc ++ )
      {
      *file << "\"" << this->Property->GetFile(cc) << "\" ";
      }
    *file << "}" << endl;

    *file << "\t" << "pvTemp" << sourceID << " Set" << this->VariableName 
          << " [ lindex $" << "pvTemp" << sourceID << "_files " 
          << this->TimeStep 
          << "]\n";
    }
  else
    {
    *file << "\t" << "pvTemp" << sourceID
          << " Set" << this->VariableName << " {" 
          << this->Entry->GetValue() << "}\n";
    }
}


//-----------------------------------------------------------------------------
void vtkPVFileEntry::AddAnimationScriptsToMenu(vtkKWMenu *menu, 
                                               vtkPVAnimationInterfaceEntry *ai)
{
  if ( this->Property->GetNumberOfFiles() > 0 )
    {
    char methodAndArgs[500];

    sprintf(methodAndArgs, "AnimationMenuCallback %s", ai->GetTclName()); 
    menu->AddCommand(this->GetTraceName(), this, methodAndArgs, 0,"");
    }
}

//-----------------------------------------------------------------------------
void vtkPVFileEntry::AnimationMenuCallback(vtkPVAnimationInterfaceEntry *ai)
{
  if (ai->InitializeTrace(NULL))
    {
    this->AddTraceEntry("$kw(%s) AnimationMenuCallback $kw(%s)", 
      this->GetTclName(), ai->GetTclName());
    }

  ai->SetLabelAndScript(this->GetTraceName(), NULL);
  ai->SetCurrentProperty(this->Property);
  ai->SetTimeStart(0);
  ai->SetTimeEnd(this->Property->GetNumberOfFiles()-1);
  ai->SetTypeToInt();
  ai->Update();
}

//----------------------------------------------------------------------------
void vtkPVFileEntry::SetProperty(vtkPVWidgetProperty *prop)
{
  this->Property = vtkPVFileEntryProperty::SafeDownCast(prop);
  if (this->Property)
    {
    char *cmd = new char[strlen(this->VariableName)+4];
    sprintf(cmd, "Set%s", this->VariableName);
    this->Property->SetVTKCommand(cmd);
    delete[] cmd;
    }
}

//----------------------------------------------------------------------------
vtkPVWidgetProperty* vtkPVFileEntry::GetProperty()
{
  return this->Property;
}

//----------------------------------------------------------------------------
vtkPVWidgetProperty* vtkPVFileEntry::CreateAppropriateProperty()
{
  return vtkPVFileEntryProperty::New();
}

//----------------------------------------------------------------------------
void vtkPVFileEntry::ExecuteEvent(vtkObject *o, unsigned long event, void* calldata)
{
  if ( event == vtkCommand::ModifiedEvent && !this->IgnoreFileListEvents )
    {
    this->UpdateTimeStep();
    this->ModifiedCallback();
    }
}

//----------------------------------------------------------------------------
void vtkPVFileEntry::UpdateTimeStep()
{
  const char* fileName = this->Entry->GetValue();
  if ( !fileName || !fileName[0] )
    {
    return;
    }

  this->IgnoreFileListEvents = 1;
  char* file = new char[ strlen(fileName) + 1 ];
  vtkKWDirectoryUtilities::GetFilenameName(fileName, file);
  this->FileListSelect->AddFinalElement(file, 1);
  this->TimeStep = this->FileListSelect->GetElementIndexFromFinalList(file);
  if ( this->TimeStep < 0 )
    {
    cout << "This should not have happended" << endl;
    cout << "Cannot find \"" << file << "\" on the list" << endl;
    int cc;
    for ( cc = 0; cc < this->FileListSelect->GetNumberOfElementsOnFinalList(); cc ++ )
      {
      cout << "Element: " << this->FileListSelect->GetElementFromFinalList(cc) << endl;
      }
    abort();
    }
  delete [] file;
  this->Timestep->SetValue(this->TimeStep);
  if ( this->FileListSelect->GetNumberOfElementsOnFinalList() > 1 )
    {
    this->Script("pack %s -side bottom -expand 1 -fill x", 
      this->TimestepFrame->GetWidgetName());
    this->Timestep->SetRange(0, 
      this->FileListSelect->GetNumberOfElementsOnFinalList()-1);
    }
  else
    {
    this->Script("pack forget %s", 
      this->TimestepFrame->GetWidgetName());
    }
  this->IgnoreFileListEvents = 0;
}

//----------------------------------------------------------------------------
void vtkPVFileEntry::UpdateAvailableFiles()
{
  if ( !this->Path )
    {
    return;
    }

  vtkPVProcessModule* pm = this->GetPVApplication()->GetProcessModule();
  vtkStringList* files = vtkStringList::New();
  pm->GetDirectoryListing(this->Path, 0, files, 0);


  this->IgnoreFileListEvents = 1;
  this->FileListSelect->RemoveItemsFromSourceList();
  int cc;
  for ( cc = 0; cc < files->GetLength(); cc ++ )
    {
    this->FileListSelect->AddSourceElement(files->GetString(cc));
    }
  this->IgnoreFileListEvents = 0;
  files->Delete();
}

//----------------------------------------------------------------------------
void vtkPVFileEntry::PrintSelf(ostream& os, vtkIndent indent)
{
  this->Superclass::PrintSelf(os, indent);
  os << "Extension: " << (this->Extension?this->Extension:"none") << endl;
}
