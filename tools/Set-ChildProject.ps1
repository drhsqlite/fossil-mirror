#!/usr/bin/env pwsh

<#PSScriptInfo

.VERSION 1.0.0

.GUID 8c36fbf7-a306-4a88-85af-078fd9c91d0f

.AUTHOR Chris Kennedy

.COMPANYNAME Fossil-SCM.org

.COPYRIGHT 2019

.TAGS Fossil-SCM

.LICENSEURI https://fossil-scm.org/fossil/doc/trunk/COPYRIGHT-BSD2.txt

.PROJECTURI https://fossil-scm.org/fossil/doc/trunk/www/index.wiki

.ICONURI

.EXTERNALMODULEDEPENDENCIES

.REQUIREDSCRIPTS

.EXTERNALSCRIPTDEPENDENCIES

.RELEASENOTES
  2019-08-09 CJK Initial release to Fossil-SCM Project.

#>

<#

.Synopsis
  Sets the current Fossil Repository to be a Child Project.

.DESCRIPTION
  Execute only in a current checkout.  Should only be ran once after the
  initial clone of the Child Project.

  Use this when you clone a repository that you should not be pushing changes
  to, such as when a repository is being used to provide a fully templated
  project.

.LINK
  https://fossil-scm.org/fossil/doc/trunk/www/childprojects.wiki

.INPUTS
  None. You cannot pipe objects to Set-ChildProject.

.OUTPUTS
  None.

.Parameter Name
  Name of the Child Project.

#>
[CmdletBinding()]
Param(
  [Parameter(Mandatory=$true,Position=0)]
  [ValidateNotNullOrEmpty()]
  [string] $Name
)

$sql = @"
UPDATE config SET name='parent-project-code' WHERE name='project-code';
UPDATE config SET name='parent-project-name' WHERE name='project-name';
INSERT INTO config(name,value) VALUES('project-code',lower(hex(randomblob(20))));
INSERT INTO config(name,value) VALUES('project-name','$Name');
"@

$sql | fossil.exe sqlite3

