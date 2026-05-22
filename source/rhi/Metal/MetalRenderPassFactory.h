#pragma once

void* createMetalColorClearRenderPassDescriptor(void* colorTexture,
                                                float red,
                                                float green,
                                                float blue,
                                                float alpha);

void* createMetalDrawableMeshRenderPassDescriptor(void* colorTexture,
                                                  void* depthTexture,
                                                  float red,
                                                  float green,
                                                  float blue,
                                                  float alpha);

void* createMetalPickRenderPassDescriptor(void* colorTexture, void* depthTexture);
