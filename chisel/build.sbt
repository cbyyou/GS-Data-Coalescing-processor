ThisBuild / scalaVersion := "2.13.16"
ThisBuild / organization := "local.datacoalescing"
ThisBuild / version := "0.1.0"

lazy val root = (project in file("."))
  .settings(
    name := "data-coalescing-chisel",
    libraryDependencies ++= Seq(
      "org.chipsalliance" %% "chisel" % "6.7.0",
      compilerPlugin("org.chipsalliance" %% "chisel-plugin" % "6.7.0" cross CrossVersion.full),
      "edu.berkeley.cs" %% "chiseltest" % "6.0.0" % Test
    ),
    scalacOptions ++= Seq("-deprecation", "-feature", "-unchecked")
  )
